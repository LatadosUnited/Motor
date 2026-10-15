#include "SceneViewport.h"

#include "PlayController.h"

#include <ImGuizmo.h>

#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace editor {

using namespace motor;

namespace {
constexpr glm::vec4 kSelectionColor{1.0f, 0.62f, 0.15f, 1.0f};
} // namespace

void SceneViewport::Init() {
    m_framebuffer = std::make_unique<Framebuffer>(1280, 720, 4);
    m_camera.position = {0.0f, 4.0f, 9.0f};
    m_camera.LookAt({0.0f, 0.5f, 0.0f});
}

void SceneViewport::UpdateCamera(float dt, Window& window) {
    if (m_looking && !Input::IsMouseDown(MouseButton::Right)) m_looking = false;

    if (m_looking) {
        m_cameraController.Update(dt, window);
    } else {
        window.SetCursorCaptured(false);
    }

    // Enquanto voa com a câmera, o ImGui não deve reagir ao mouse.
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiConfigFlags mouseFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
    if (m_looking) {
        io.ConfigFlags |= mouseFlags;
    } else {
        io.ConfigFlags &= ~mouseFlags;
    }
}

void SceneViewport::Render(Renderer& renderer, const EditorContext& ctx, bool gameView) {
    m_framebuffer->Resize(static_cast<int>(m_size.x), static_cast<int>(m_size.y));
    m_camera.aspect = m_framebuffer->AspectRatio();

    const Scene& scene = ctx.scene;
    // No Play, pela câmera principal da cena; sem ela, pela câmera do editor (com aviso no viewport).
    Camera camera = m_camera;
    m_missingGameCamera = gameView && !scene.ApplyCamera(scene.FindMainCamera(), camera);
    renderer.BeginScene(camera, scene.light, scene.environment, m_framebuffer.get());
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
        DrawGizmo(ctx, origin);
        DrawToolbar(ImVec2(origin.x + 8.0f, origin.y + 44.0f));
    }
    play.DrawControls(origin, m_size.x);

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
            PickAt(ctx, {(mouse.x - origin.x) / m_size.x, (mouse.y - origin.y) / m_size.y});
        }
    }

    // Overlay com estatísticas.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char text[160];
    std::snprintf(text, sizeof(text), "%.0f FPS  |  %u draws  |  %u tris  |  %dx%d", fps, stats.drawCalls,
                  stats.triangles, m_framebuffer->Width(), m_framebuffer->Height());
    const ImVec2 pad{8.0f, 6.0f};
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 p0{origin.x + 8.0f, origin.y + 8.0f};
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

void SceneViewport::DrawGizmo(EditorContext& ctx, const ImVec2& origin) {
    const EntityId selected = ctx.selected;
    if (m_gizmoTool == GizmoTool::None || !ctx.scene.Find(selected)) return;

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetGizmoSizeClipSpace(0.15f);  // padrão 0.1 fica pequeno no viewport
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(origin.x, origin.y, m_size.x, m_size.y);
    ImGuizmo::Enable(!m_looking);

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    float snapStep = m_snapTranslate;
    if (m_gizmoTool == GizmoTool::Rotate) {
        op = ImGuizmo::ROTATE;
        snapStep = m_snapRotate;
    } else if (m_gizmoTool == GizmoTool::Scale) {
        op = ImGuizmo::SCALE;
        snapStep = m_snapScale;
    }
    const ImGuizmo::MODE mode = m_gizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

    // Snap fixo pelo botão da barra, ou temporário segurando Ctrl.
    const bool snap = m_snap || ImGui::GetIO().KeyCtrl;
    const float snapValues[3] = {snapStep, snapStep, snapStep};

    const glm::mat4 view = m_camera.View();
    const glm::mat4 projection = m_camera.Projection();
    glm::mat4 world = ctx.scene.WorldMatrix(selected);

    if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), op, mode, glm::value_ptr(world),
                             nullptr, snap ? snapValues : nullptr)) {
        ctx.scene.SetWorldMatrix(selected, world);
    }
}

void SceneViewport::DrawToolbar(const ImVec2& position) {
    struct ToolButton { const char* label; GizmoTool tool; const char* tip; };
    const ToolButton tools[] = {
        {"Selecionar", GizmoTool::None, "Sem gizmo (Q)"},
        {"Mover", GizmoTool::Translate, "Mover (W)"},
        {"Rotacionar", GizmoTool::Rotate, "Rotacionar (E)"},
        {"Escalar", GizmoTool::Scale, "Escalar (R)"},
    };

    const ImVec4 activeColor = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    auto toggleButton = [&](const char* label, bool active) {
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
        const bool clicked = ImGui::Button(label);
        if (active) ImGui::PopStyleColor();
        return clicked;
    };

    // Fundo translúcido desenhado atrás dos botões (canal 0) depois de sabermos o tamanho do grupo.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);

    ImGui::SetCursorScreenPos(ImVec2(position.x + 6.0f, position.y + 5.0f));
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
    for (const ToolButton& t : tools) {
        if (toggleButton(t.label, m_gizmoTool == t.tool)) m_gizmoTool = t.tool;
        ImGui::SetItemTooltip("%s", t.tip);
        ImGui::SameLine();
    }
    ImGui::SameLine(0.0f, 14.0f);
    if (toggleButton(m_gizmoLocal ? "Local" : "Global", false)) m_gizmoLocal = !m_gizmoLocal;
    ImGui::SetItemTooltip("Eixos do gizmo: locais do objeto ou do mundo (X)");
    ImGui::SameLine();
    if (toggleButton("Snap", m_snap)) m_snap = !m_snap;
    ImGui::SetItemTooltip("Encaixe: %.2g m / %.0f° / %.2g (segure Ctrl para ativar temporariamente)",
                          m_snapTranslate, m_snapRotate, m_snapScale);
    ImGui::PopStyleVar();
    ImGui::EndGroup();

    const ImVec2 groupMax = ImGui::GetItemRectMax();
    dl->ChannelsSetCurrent(0);
    dl->AddRectFilled(position, ImVec2(groupMax.x + 6.0f, groupMax.y + 5.0f), IM_COL32(0, 0, 0, 120), 4.0f);
    dl->ChannelsMerge();
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
    const auto [origin, direction] = Ray(uv);
    const auto hit = ctx.scene.Raycast(origin, direction);
    ctx.selected = hit ? hit->entity : kNullEntity;
}

void SceneViewport::HandleDrop(EditorContext& ctx, const ImVec2& origin) {
    // Arquivos do painel Projeto: malha = nova entidade onde o mouse apontar; textura = no objeto sob o mouse.
    const auto meshes = AcceptPathDrop(AssetType::Mesh);
    const auto textures = AcceptPathDrop(AssetType::Texture);
    if (meshes.empty() && textures.empty()) return;

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
