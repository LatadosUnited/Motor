// Viewport da cena: câmera livre, imagem, seleção por clique e arquivos soltos. O que é desenhado por
// cima (ícones, colliders, gizmo, barra de ferramentas) fica em SceneViewportOverlay.cpp.

#include "viewport/SceneViewport.h"

#include "play/PlayController.h"
#include "viewport/PerceptionOverlay.h"

#include <ImGuizmo.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace editor {

using namespace motor;

namespace {
constexpr glm::vec4 kSelectionColor{1.0f, 0.62f, 0.15f, 1.0f};
constexpr float kIconPickRadius = 14.0f;  // tolerância do clique
constexpr float kBoxMinDrag = 4.0f;       // pixels arrastados até o clique virar retângulo de seleção
constexpr ImU32 kBoxFill = IM_COL32(255, 158, 38, 40);
constexpr ImU32 kBoxBorder = IM_COL32(255, 158, 38, 200);
// Linhas para cada lado do centro: a camada fina vai 20 células longe, a grossa 10 (de 10 células cada). Mais que
// isso vira ruído perto do horizonte (as linhas não somem com a distância).
constexpr int kGridMinorHalfCount = 20;
constexpr int kGridMajorHalfCount = 10;
constexpr glm::vec4 kGridMinorColor{0.42f, 0.44f, 0.48f, 1.0f};
constexpr glm::vec4 kGridMajorColor{0.62f, 0.64f, 0.68f, 1.0f};
constexpr glm::vec4 kGridAxisX{0.85f, 0.3f, 0.3f, 1.0f};
constexpr glm::vec4 kGridAxisZ{0.3f, 0.45f, 0.9f, 1.0f};

// Ctrl ou Shift: o clique e o retângulo somam à seleção em vez de trocá-la.
bool AddingToSelection() {
    const ImGuiIO& io = ImGui::GetIO();
    return io.KeyCtrl || io.KeyShift;
}
} // namespace

void SceneViewport::Init() {
    // Sem MSAA: o renderer desenha a cena num alvo HDR próprio, com MSAA, e entrega a imagem pronta aqui.
    m_framebuffer = std::make_unique<Framebuffer>(1280, 720, 1);
    m_ui = std::make_unique<UiRenderer>();
    m_camera.position = {0.0f, 4.0f, 9.0f};
    m_camera.LookAt({0.0f, 0.5f, 0.0f});
    m_gridMesh = CreateGridMesh(kGridMinorHalfCount);
    m_gridMajorMesh = CreateGridMesh(kGridMajorHalfCount);
    const Vertex axis[2] = {{{-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
                            {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}}};
    const uint32_t axisIndices[3] = {0, 1, 1};
    m_axisMesh = std::make_shared<Mesh>(axis, axisIndices);
}

void SceneViewport::SubmitGrid(Renderer& renderer) const {
    // Duas camadas: células do passo do mover e linhas mais fortes a cada 10. De longe, as duas sobem de 10 em
    // 10 (senão a grade fina vira uma mancha e a de 50 células acaba antes do horizonte).
    const float height = std::abs(m_camera.position.y - m_prefs.gridHeight);
    float minor = m_prefs.snapMove;
    for (int i = 0; i < 4 && height > minor * 15.0f; ++i) minor *= 10.0f;
    const float major = minor * 10.0f;

    auto layer = [&](const Mesh& mesh, float cell, float alignTo, const glm::vec4& color) {
        const glm::vec3 center = GridCenter(m_camera.position, alignTo, m_prefs.gridHeight);
        const glm::mat4 model = glm::scale(glm::translate(glm::mat4(1.0f), center), glm::vec3(cell, 1.0f, cell));
        renderer.SubmitWireframe(mesh, model, color);
    };
    // As finas enchem a tela de quadradinhos perto da câmera: só quando pedidas.
    if (m_prefs.gridFine) layer(*m_gridMesh, minor, major, kGridMinorColor);
    layer(*m_gridMajorMesh, major, major, kGridMajorColor);

    // Eixos X (vermelho) e Z (azul) do mundo, na altura da grade.
    const float extent = major * static_cast<float>(kGridMajorHalfCount);
    const glm::mat4 base = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, m_prefs.gridHeight, 0.0f));
    renderer.SubmitWireframe(*m_axisMesh, glm::scale(base, glm::vec3(extent, 1.0f, 1.0f)), kGridAxisX);
    const glm::mat4 alongZ = glm::rotate(base, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    renderer.SubmitWireframe(*m_axisMesh, glm::scale(alongZ, glm::vec3(extent, 1.0f, 1.0f)), kGridAxisZ);
}

void SceneViewport::UpdateCamera(float dt, Window& window) {
    if (m_looking && !Input::IsMouseDown(MouseButton::Right)) m_looking = false;

    // Enquanto voa com a câmera, o ImGui não deve reagir ao mouse. Ao parar de voar, solta o cursor uma vez só:
    // no resto do tempo quem decide é o jogo no Play (cursor preso por script, PlayController).
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiConfigFlags mouseFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
    if (m_looking) {
        m_cameraController.Update(dt, window);
        io.ConfigFlags |= mouseFlags;
    } else if (m_wasLooking) {
        window.SetCursorCaptured(false);
        io.ConfigFlags &= ~mouseFlags;
    }
    m_wasLooking = m_looking;
}

void SceneViewport::Render(Renderer& renderer, const EditorContext& ctx, bool gameView) {
    m_framebuffer->Resize(static_cast<int>(m_size.x), static_cast<int>(m_size.y));
    m_camera.aspect = m_framebuffer->AspectRatio();
    m_missingGameCamera = !RenderTo(renderer, ctx, gameView, *m_framebuffer);
}

bool SceneViewport::RenderTo(Renderer& renderer, const EditorContext& ctx, bool gameView, const Framebuffer& target) {
    const Scene& scene = ctx.scene;
    // No Play, pela câmera principal da cena; sem ela, pela câmera do editor (com aviso no viewport).
    Camera camera = m_camera;
    camera.aspect = target.AspectRatio();
    const bool missingGameCamera = gameView && !scene.ApplyCamera(scene.FindMainCamera(), camera);
    renderer.BeginScene(camera, scene.CollectLights(), scene.environment, &target);
    scene.SubmitTo(renderer);

    // Contorno em wireframe da seleção (e de seus descendentes). A visão do jogo fica limpa.
    auto inSelection = [&](EntityId id) {
        return std::any_of(ctx.selection.begin(), ctx.selection.end(),
                           [&](EntityId s) { return id == s || scene.IsDescendantOf(id, s); });
    };
    for (const auto& e : scene.Entities()) {
        if (gameView || ctx.selection.empty()) break;
        const MeshRenderer* mr = e->Get<MeshRenderer>();
        if (!mr || !mr->mesh) continue;
        if (!inSelection(e->id)) continue;
        renderer.SubmitWireframe(*mr->mesh, scene.WorldMatrix(e->id), kSelectionColor);
    }
    if (!gameView && m_prefs.grid) SubmitGrid(renderer);
    if (!gameView && m_prefs.navMesh) m_navOverlay.Submit(renderer);
    renderer.EndScene();

    // UI do jogo por cima, no mesmo framebuffer (no Play e na edição, para ver o HUD no lugar).
    if (m_showUi) m_ui->Render(scene, ctx.assets, target.Width(), target.Height(), &target);
    return !missingGameCamera;
}

void SceneViewport::Draw(EditorContext& ctx, PlayController& play, const RenderStats& stats, float fps) {
    if (std::exchange(m_focusRequest, false)) ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin("Viewport");
    ImGui::PopStyleVar();
    m_focused = visible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!visible) {
        ImGui::End();
        return;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    m_size = {std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)};

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const auto texture = static_cast<ImTextureID>(static_cast<uintptr_t>(m_framebuffer->ColorTexture()));
    // O OpenGL guarda as linhas de baixo para cima: invertemos o V.
    ImGui::Image(ImTextureRef(texture), ImVec2(m_size.x, m_size.y), ImVec2(0, 1), ImVec2(1, 0));
    const bool imageHovered = ImGui::IsItemHovered();
    HandleDrop(ctx, origin);

    // Na visão do jogo não há gizmo, seleção pelo viewport nem câmera livre: o mouse é do jogo.
    const bool gameView = play.InGameView();
    // A malha do navmesh vale para o próximo Render (o quadro da cena é desenhado antes desta janela).
    m_navOverlay.Update(ctx, play.Runtime());
    if (!gameView) {
        CollectIcons(ctx);
        ImDrawList* overlay = ImGui::GetWindowDrawList();
        if (m_showUi) m_uiTool.Draw(ctx, overlay, origin, m_ui->Canvas().Layout());
        DrawColliders(ctx, overlay, origin);
        if (m_prefs.navMesh) {
            m_navOverlay.DrawPaths(ctx, play.Runtime(), overlay, origin, [this](const glm::vec3& p) { return Project(p); });
            DrawPerception(ctx, play.Runtime(), overlay, origin, [this](const glm::vec3& p) { return Project(p); });
        }
        DrawSelectedHelpers(ctx, overlay, origin);
        DrawIcons(ctx, overlay, origin);
        DrawGizmo(ctx, origin);
        DrawToolbar(ImVec2(origin.x + 8.0f, origin.y + 44.0f));
    }
    play.DrawControls(origin, m_size.x);

    // Mouse para a UI do jogo (botões): só na visão do jogo e fora dos botões de Play do viewport. Lado a lado,
    // quem informa é a janela Jogo (GameView).
    if (SceneRuntime* runtime = play.SideBySide() ? nullptr : play.Runtime()) {
        if (UiSystem* ui = runtime->GetSystem<UiSystem>()) {
            const ImVec2 mouse = ImGui::GetMousePos();
            ui->SetPointer({m_size, {mouse.x - origin.x, mouse.y - origin.y},
                            gameView && m_showUi && imageHovered && !ImGui::IsAnyItemHovered(),
                            ImGui::IsMouseDown(ImGuiMouseButton_Left)});
        }
    }

    // Cliques que caem no gizmo ou na barra de ferramentas não chegam à cena.
    const bool sceneHovered = !gameView && imageHovered && !ImGui::IsAnyItemHovered() &&
                              !ImGuizmo::IsOver() && !ImGuizmo::IsUsing();
    if (sceneHovered) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_looking = true;
            ImGui::SetWindowFocus();
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetMousePos();
            const glm::vec2 local{mouse.x - origin.x, mouse.y - origin.y};
            // Ícones de luz/câmera ficam por cima de tudo; depois a UI (desenhada por cima da cena); depois as malhas.
            const bool onIcon = std::any_of(m_icons.begin(), m_icons.end(), [&](const Icon& icon) {
                const float dx = icon.center.x - local.x, dy = icon.center.y - local.y;
                return dx * dx + dy * dy <= kIconPickRadius * kIconPickRadius;
            });
            if (onIcon || !m_showUi || !m_uiTool.OnClick(ctx, local, m_ui->Canvas().Layout())) {
                // Seleciona ao soltar: sem arrastar é um clique; arrastando, um retângulo.
                m_boxPending = true;
                m_boxStart = ImVec2(local.x, local.y);
            }
        }
    }
    if (m_boxPending) UpdateBoxSelection(ctx, origin, gameView);
    if (!gameView) m_uiTool.Update(ctx, m_size);

    // Overlay com estatísticas.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char text[200];
    std::snprintf(text, sizeof(text), "%.0f FPS  |  %u draws (sombras: %u em %u passes)  |  %u tris  |  UI: %u draws  |  %dx%d",
                  fps, stats.drawCalls, stats.shadowDrawCalls, stats.shadowPasses, stats.triangles,
                  m_showUi ? m_ui->LastDrawCalls() : 0u, m_framebuffer->Width(), m_framebuffer->Height());
    const ImVec2 pad{8.0f, 6.0f};
    const ImVec2 size = ImGui::CalcTextSize(text);
    // Na visão do jogo, embaixo: o canto de cima costuma ser do HUD do jogo.
    const ImVec2 p0{origin.x + 8.0f, gameView ? origin.y + m_size.y - size.y - pad.y * 2 - 8.0f : origin.y + 8.0f};
    dl->AddRectFilled(p0, ImVec2(p0.x + size.x + pad.x * 2, p0.y + size.y + pad.y * 2), IM_COL32(0, 0, 0, 120), 4.0f);
    dl->AddText(ImVec2(p0.x + pad.x, p0.y + pad.y), IM_COL32(235, 235, 235, 255), text);
    // Contadores à direita, abaixo dos botões de Play e (fora da visão do jogo) da barra de ferramentas.
    if (m_prefs.stats) {
        const float top = gameView ? 44.0f : 84.0f;
        m_statsPanel.Draw(dl, ImVec2(origin.x + m_size.x - 8.0f, origin.y + top), m_size.y - top - 8.0f);
    }

    // Indicação de Play: borda colorida em volta da imagem, e aviso se a cena não tem câmera.
    if (play.IsActive()) {
        const ImVec2 p1{origin.x + m_size.x, origin.y + m_size.y};
        dl->AddRect(ImVec2(origin.x + 1.5f, origin.y + 1.5f), ImVec2(p1.x - 1.5f, p1.y - 1.5f), play.StateColor(),
                    0.0f, 0, 3.0f);
        const char* warning = nullptr;
        if (m_missingGameCamera) {
            warning = "Nenhuma câmera ativa na cena: mostrando pela câmera do editor (adicione o componente Câmera)";
        } else if (!gameView && !play.SideBySide()) {  // lado a lado, o jogo está na janela Jogo
            warning = "Vendo pela câmera do editor (F8 volta para a câmera do jogo)";
        }
        if (warning) {
            const ImVec2 ws = ImGui::CalcTextSize(warning);
            const ImVec2 wp{origin.x + (m_size.x - ws.x) * 0.5f, p1.y - ws.y - 16.0f};
            dl->AddRectFilled(ImVec2(wp.x - 8.0f, wp.y - 5.0f), ImVec2(wp.x + ws.x + 8.0f, wp.y + ws.y + 5.0f),
                              IM_COL32(0, 0, 0, 150), 4.0f);
            dl->AddText(wp, IM_COL32(255, 220, 120, 255), warning);
        }
    }

    ImGui::End();
}

std::optional<ImVec2> SceneViewport::Project(const glm::vec3& world) const {
    const glm::vec4 clip = m_camera.Projection() * m_camera.View() * glm::vec4(world, 1.0f);
    if (clip.w <= m_camera.nearPlane) return std::nullopt;
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return ImVec2((ndc.x * 0.5f + 0.5f) * m_size.x, (0.5f - ndc.y * 0.5f) * m_size.y);
}

void SceneViewport::HandleGizmoShortcuts() {
    // Ferramentas de transform (padrão Unity). Não conflita com WASD, que só vale com o botão direito segurado.
    if (ImGui::GetIO().KeyCtrl) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) m_gizmoTool = GizmoTool::None;
    if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_gizmoTool = GizmoTool::Translate;
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_gizmoTool = GizmoTool::Rotate;
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_gizmoTool = GizmoTool::Scale;
    if (ImGui::IsKeyPressed(ImGuiKey_X, false)) m_gizmoLocal = !m_gizmoLocal;
}

std::pair<glm::vec3, glm::vec3> SceneViewport::Ray(const glm::vec2& uv) const {
    const glm::vec2 ndc{uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f};
    const glm::mat4 invViewProj = glm::inverse(m_camera.Projection() * m_camera.View());

    glm::vec4 nearPoint = invViewProj * glm::vec4(ndc, -1.0f, 1.0f);
    glm::vec4 farPoint = invViewProj * glm::vec4(ndc, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    return {glm::vec3(nearPoint), glm::vec3(farPoint - nearPoint)};
}

void SceneViewport::PickAt(EditorContext& ctx, const glm::vec2& uv) {
    // Ícones primeiro (ficam por cima da imagem); entre vários sob o mouse, o mais perto da câmera.
    const ImVec2 mouse{uv.x * m_size.x, uv.y * m_size.y};
    const Icon* best = nullptr;
    for (const Icon& icon : m_icons) {
        const float dx = icon.center.x - mouse.x, dy = icon.center.y - mouse.y;
        if (dx * dx + dy * dy > kIconPickRadius * kIconPickRadius) continue;
        if (!best || icon.depth < best->depth) best = &icon;
    }
    EntityId picked = kNullEntity;
    if (best) {
        picked = best->entity;
    } else {
        const auto [origin, direction] = Ray(uv);
        const auto hit = ctx.scene.Raycast(origin, direction);
        if (hit) picked = hit->entity;
    }
    if (!AddingToSelection()) {
        ctx.SelectOnly(picked);
    } else if (picked != kNullEntity) {
        ctx.ToggleSelected(picked);  // Ctrl/Shift+clique no vazio mantém a seleção
    }
}

void SceneViewport::UpdateBoxSelection(EditorContext& ctx, const ImVec2& origin, bool gameView) {
    if (gameView || m_looking) {
        m_boxPending = false;
        return;
    }
    const ImVec2 mouse = ImGui::GetMousePos();
    const ImVec2 end{std::clamp(mouse.x - origin.x, 0.0f, m_size.x), std::clamp(mouse.y - origin.y, 0.0f, m_size.y)};
    const bool dragged = std::abs(end.x - m_boxStart.x) > kBoxMinDrag || std::abs(end.y - m_boxStart.y) > kBoxMinDrag;
    const ImVec2 lo{std::min(m_boxStart.x, end.x), std::min(m_boxStart.y, end.y)};
    const ImVec2 hi{std::max(m_boxStart.x, end.x), std::max(m_boxStart.y, end.y)};
    if (dragged) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a{origin.x + lo.x, origin.y + lo.y}, b{origin.x + hi.x, origin.y + hi.y};
        dl->AddRectFilled(a, b, kBoxFill);
        dl->AddRect(a, b, kBoxBorder);
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;

    m_boxPending = false;
    if (!dragged) {
        PickAt(ctx, {m_boxStart.x / m_size.x, m_boxStart.y / m_size.y});
        return;
    }
    // Entra quem tem o centro (da malha, ou o ícone de luz/câmera) dentro do retângulo. Elementos de UI,
    // entidades vazias e inativas ficam de fora.
    std::vector<EntityId> inside;
    for (const auto& e : ctx.scene.Entities()) {
        if (!ctx.scene.IsActiveInHierarchy(e->id) || IsUiEntity(*e)) continue;
        const glm::mat4 world = ctx.scene.WorldMatrix(e->id);
        glm::vec3 center{world[3]};
        const MeshRenderer* mr = e->Get<MeshRenderer>();
        if (mr && mr->mesh) {
            center = glm::vec3(world * glm::vec4((mr->mesh->BoundsMin() + mr->mesh->BoundsMax()) * 0.5f, 1.0f));
        } else if (!e->Has<LightComponent>() && !e->Has<CameraComponent>()) {
            continue;
        }
        const auto p = Project(center);
        if (p && p->x >= lo.x && p->x <= hi.x && p->y >= lo.y && p->y <= hi.y) inside.push_back(e->id);
    }
    if (!AddingToSelection()) ctx.SelectOnly(kNullEntity);
    for (EntityId id : inside) ctx.AddToSelection(id);
}

void SceneViewport::HandleDrop(EditorContext& ctx, const ImVec2& origin) {
    // Arquivos do painel Projeto: malha ou prefab = nova entidade onde o mouse apontar; textura ou material =
    // no objeto sob o mouse.
    const auto meshes = AcceptPathDrop(AssetType::Mesh);
    const auto textures = AcceptPathDrop(AssetType::Texture);
    const auto prefabs = AcceptPathDrop(AssetType::Prefab);
    const auto materials = AcceptPathDrop(AssetType::Material);
    const auto models = AcceptPathDrop(AssetType::Model);
    if (meshes.empty() && textures.empty() && prefabs.empty() && materials.empty() && models.empty()) return;

    const ImVec2 mouse = ImGui::GetMousePos();
    const auto [rayOrigin, rayDir] = Ray({(mouse.x - origin.x) / m_size.x, (mouse.y - origin.y) / m_size.y});
    const auto hit = ctx.scene.Raycast(rayOrigin, rayDir);

    if (!textures.empty()) {
        if (hit) {
            ctx.ApplyTextureFile(hit->entity, textures.front());
        } else {
            ctx.pendingError = "Solte a textura sobre um objeto da cena.";
        }
    }
    if (!materials.empty()) {
        if (hit) {
            ctx.ApplyMaterialFile(hit->entity, materials.front());
        } else {
            ctx.pendingError = "Solte o material sobre um objeto da cena.";
        }
    }

    // Ponto de apoio: superfície atingida, senão o chão (y = 0), senão 10 m à frente.
    glm::vec3 point = rayOrigin + glm::normalize(rayDir) * 10.0f;
    if (hit) {
        point = rayOrigin + rayDir * hit->distance;
    } else if (rayDir.y < -1e-4f) {
        const float t = -rayOrigin.y / rayDir.y;
        if (glm::length(rayDir * t) < 200.0f) point = rayOrigin + rayDir * t;
    }

    float offset = 0.0f;
    for (const auto& file : meshes) {
        if (Entity* e = ctx.CreateFromMeshFile(file)) {
            const Mesh& mesh = *e->Get<MeshRenderer>()->mesh;
            // Apoia a base da malha no ponto (a origem do OBJ nem sempre fica embaixo).
            e->transform.position = point + glm::vec3(offset, -mesh.BoundsMin().y, 0.0f);
            offset += std::max(1.0f, mesh.BoundsMax().x - mesh.BoundsMin().x) * 1.2f;
        }
    }
    for (const auto& file : prefabs) {
        if (Entity* e = ctx.InstantiatePrefabFile(file)) {
            e->transform.position = point + glm::vec3(offset, 0.0f, 0.0f);
            offset += 1.5f;
        }
    }
    for (const auto& file : models) {  // glTF: instância do prefab gerado (importa na hora se preciso)
        if (Entity* e = ctx.InstantiateModelFile(file)) {
            e->transform.position = point + glm::vec3(offset, 0.0f, 0.0f);
            offset += 1.5f;
        }
    }
}

void SceneViewport::FocusSelected(const EditorContext& ctx) {
    if (!ctx.scene.Find(ctx.selected)) return;

    const glm::mat4 world = ctx.scene.WorldMatrix(ctx.selected);
    const glm::vec3 target{world[3]};
    const float size = std::max({glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])),
                                 glm::length(glm::vec3(world[2]))});
    const float distance = std::clamp(size * 2.5f, 2.0f, 60.0f);
    m_camera.position = target - m_camera.Forward() * distance;
}

} // namespace editor
