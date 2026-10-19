// Viewport da cena: câmera livre, imagem, seleção por clique e arquivos soltos. O que é desenhado por
// cima (ícones, colliders, gizmo, barra de ferramentas) fica em SceneViewportOverlay.cpp.

#include "viewport/SceneViewport.h"

#include "play/PlayController.h"

#include <ImGuizmo.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
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
} // namespace

void SceneViewport::Init() {
    // Sem MSAA: o renderer desenha a cena num alvo HDR próprio, com MSAA, e entrega a imagem pronta aqui.
    m_framebuffer = std::make_unique<Framebuffer>(1280, 720, 1);
    m_ui = std::make_unique<UiRenderer>();
    m_camera.position = {0.0f, 4.0f, 9.0f};
    m_camera.LookAt({0.0f, 0.5f, 0.0f});
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

    const Scene& scene = ctx.scene;
    // No Play, pela câmera principal da cena; sem ela, pela câmera do editor (com aviso no viewport).
    Camera camera = m_camera;
    m_missingGameCamera = gameView && !scene.ApplyCamera(scene.FindMainCamera(), camera);
    renderer.BeginScene(camera, scene.CollectLights(), scene.environment, m_framebuffer.get());
    scene.SubmitTo(renderer);

    // Contorno em wireframe da seleção (e de seus descendentes). A visão do jogo fica limpa.
    for (const auto& e : scene.Entities()) {
        if (gameView) break;
        const MeshRenderer* mr = e->Get<MeshRenderer>();
        if (!mr || !mr->mesh) continue;
        if (e->id != ctx.selected && !scene.IsDescendantOf(e->id, ctx.selected)) continue;
        renderer.SubmitWireframe(*mr->mesh, scene.WorldMatrix(e->id), kSelectionColor);
    }
    renderer.EndScene();

    // UI do jogo por cima, no mesmo framebuffer (no Play e na edição, para ver o HUD no lugar).
    if (m_showUi) m_ui->Render(scene, ctx.assets, m_framebuffer->Width(), m_framebuffer->Height(), m_framebuffer.get());
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
    if (!gameView) {
        CollectIcons(ctx);
        ImDrawList* overlay = ImGui::GetWindowDrawList();
        if (m_showUi) m_uiTool.Draw(ctx, overlay, origin, m_ui->Canvas().Layout());
        DrawColliders(ctx, overlay, origin);
        DrawSelectedHelpers(ctx, overlay, origin);
        DrawIcons(ctx, overlay, origin);
        DrawGizmo(ctx, origin);
        DrawToolbar(ImVec2(origin.x + 8.0f, origin.y + 44.0f));
    }
    play.DrawControls(origin, m_size.x);

    // Mouse para a UI do jogo (botões): só na visão do jogo e fora dos botões de Play do viewport.
    if (SceneRuntime* runtime = play.Runtime()) {
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
                PickAt(ctx, {local.x / m_size.x, local.y / m_size.y});
            }
        }
    }
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

    // Indicação de Play: borda colorida em volta da imagem, e aviso se a cena não tem câmera.
    if (play.IsActive()) {
        const ImVec2 p1{origin.x + m_size.x, origin.y + m_size.y};
        dl->AddRect(ImVec2(origin.x + 1.5f, origin.y + 1.5f), ImVec2(p1.x - 1.5f, p1.y - 1.5f), play.StateColor(),
                    0.0f, 0, 3.0f);
        const char* warning = nullptr;
        if (m_missingGameCamera) {
            warning = "Nenhuma câmera ativa na cena: mostrando pela câmera do editor (adicione o componente Câmera)";
        } else if (!gameView) {
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
    if (best) {
        ctx.selected = best->entity;
        return;
    }

    const auto [origin, direction] = Ray(uv);
    const auto hit = ctx.scene.Raycast(origin, direction);
    ctx.selected = hit ? hit->entity : kNullEntity;
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
