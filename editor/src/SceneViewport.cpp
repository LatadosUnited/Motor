#include "SceneViewport.h"

#include "PlayController.h"

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
constexpr ImU32 kSelectionColorU32 = IM_COL32(255, 158, 38, 255);
constexpr ImU32 kHelperColor = IM_COL32(255, 230, 140, 200);
constexpr ImU32 kColliderColor = IM_COL32(110, 230, 120, 150);
constexpr ImU32 kColliderSelectedColor = IM_COL32(140, 255, 150, 255);
constexpr ImU32 kTriggerColor = IM_COL32(110, 200, 255, 150);  // triggers em azul, para diferenciar
constexpr float kIconRadius = 11.0f;      // raio do ícone em pixels
constexpr float kIconPickRadius = 14.0f;  // tolerância do clique

ImVec2 Add(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }

ImU32 IconColor(const glm::vec3& color) {
    // Cor da luz com um piso, para o ícone de uma luz escura continuar visível.
    const glm::vec3 c = glm::clamp(glm::max(color, glm::vec3(0.25f)), 0.0f, 1.0f);
    return IM_COL32(static_cast<int>(c.r * 255.0f), static_cast<int>(c.g * 255.0f), static_cast<int>(c.b * 255.0f), 255);
}

void DrawSunIcon(ImDrawList* dl, const ImVec2& c, ImU32 color) {
    for (int i = 0; i < 8; ++i) {
        const float a = static_cast<float>(i) * 0.785398f;
        const ImVec2 dir{std::cos(a), std::sin(a)};
        dl->AddLine(ImVec2(c.x + dir.x * 7.5f, c.y + dir.y * 7.5f), ImVec2(c.x + dir.x * 11.0f, c.y + dir.y * 11.0f),
                    color, 2.0f);
    }
    dl->AddCircleFilled(c, 5.5f, color);
}

void DrawBulbIcon(ImDrawList* dl, const ImVec2& c, ImU32 color) {
    dl->AddCircleFilled(ImVec2(c.x, c.y - 2.0f), 7.0f, color);
    dl->AddRectFilled(ImVec2(c.x - 3.5f, c.y + 4.0f), ImVec2(c.x + 3.5f, c.y + 9.0f), IM_COL32(170, 170, 170, 255), 1.5f);
}

void DrawSpotIcon(ImDrawList* dl, const ImVec2& c, ImU32 color) {
    // Holofote visto de lado: corpo e o feixe abrindo para baixo.
    dl->AddRectFilled(ImVec2(c.x - 4.0f, c.y - 9.0f), ImVec2(c.x + 4.0f, c.y - 2.0f), IM_COL32(170, 170, 170, 255), 1.5f);
    dl->AddQuadFilled(ImVec2(c.x - 4.0f, c.y - 2.0f), ImVec2(c.x + 4.0f, c.y - 2.0f), ImVec2(c.x + 9.0f, c.y + 9.0f),
                      ImVec2(c.x - 9.0f, c.y + 9.0f), color);
}

void DrawCameraIcon(ImDrawList* dl, const ImVec2& c, ImU32 color) {
    dl->AddRectFilled(ImVec2(c.x - 10.0f, c.y - 6.0f), ImVec2(c.x + 4.0f, c.y + 6.0f), color, 2.0f);
    dl->AddTriangleFilled(ImVec2(c.x + 4.0f, c.y), ImVec2(c.x + 11.0f, c.y - 6.0f), ImVec2(c.x + 11.0f, c.y + 6.0f), color);
}
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
        if (m_showColliders) DrawColliders(ctx, overlay, origin);
        DrawSelectedHelpers(ctx, overlay, origin);
        DrawIcons(ctx, overlay, origin);
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

std::optional<ImVec2> SceneViewport::Project(const glm::vec3& world) const {
    const glm::vec4 clip = m_camera.Projection() * m_camera.View() * glm::vec4(world, 1.0f);
    if (clip.w <= m_camera.nearPlane) return std::nullopt;
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return ImVec2((ndc.x * 0.5f + 0.5f) * m_size.x, (0.5f - ndc.y * 0.5f) * m_size.y);
}

void SceneViewport::CollectIcons(const EditorContext& ctx) {
    m_icons.clear();
    const Scene& scene = ctx.scene;
    for (const auto& e : scene.Entities()) {
        if (!e->Has<LightComponent>() && !e->Has<CameraComponent>()) continue;
        const glm::vec3 position{scene.WorldMatrix(e->id)[3]};
        const auto screen = Project(position);
        if (!screen || screen->x < -kIconRadius || screen->y < -kIconRadius || screen->x > m_size.x + kIconRadius ||
            screen->y > m_size.y + kIconRadius) {
            continue;
        }
        m_icons.push_back({e->id, *screen, glm::length(position - m_camera.position)});
    }
    // Os mais distantes primeiro: o mais perto fica desenhado por cima.
    std::sort(m_icons.begin(), m_icons.end(), [](const Icon& a, const Icon& b) { return a.depth > b.depth; });
}

void SceneViewport::DrawIcons(const EditorContext& ctx, ImDrawList* dl, const ImVec2& origin) const {
    for (const Icon& icon : m_icons) {
        const Entity* e = ctx.scene.Find(icon.entity);
        if (!e) continue;
        const ImVec2 c = Add(origin, icon.center);
        const bool active = ctx.scene.IsActiveInHierarchy(icon.entity);

        // Fundo escuro redondo para o ícone se destacar de qualquer cena; entidade inativa fica apagada.
        dl->AddCircleFilled(c, kIconRadius + 3.0f, IM_COL32(20, 20, 24, active ? 170 : 90));
        if (icon.entity == ctx.selected) dl->AddCircle(c, kIconRadius + 3.0f, kSelectionColorU32, 0, 2.0f);

        const ImU32 inactive = IM_COL32(120, 120, 120, 255);
        if (const LightComponent* light = e->Get<LightComponent>()) {
            const ImU32 color = active ? IconColor(light->color) : inactive;
            switch (light->type) {
                case LightType::Directional: DrawSunIcon(dl, c, color); break;
                case LightType::Point: DrawBulbIcon(dl, c, color); break;
                case LightType::Spot: DrawSpotIcon(dl, c, color); break;
            }
        } else {
            DrawCameraIcon(dl, c, active ? IM_COL32(210, 215, 225, 255) : inactive);
        }
    }
}

void SceneViewport::DrawSelectedHelpers(const EditorContext& ctx, ImDrawList* dl, const ImVec2& origin) const {
    const Entity* e = ctx.scene.Find(ctx.selected);
    if (!e) return;
    const glm::mat4 world = ctx.scene.WorldMatrix(e->id);
    const glm::vec3 position{world[3]};
    glm::vec3 forward = -glm::vec3(world[2]);
    if (glm::length(forward) < 1e-6f) return;
    forward = glm::normalize(forward);
    // Base ortonormal em volta da direção, para desenhar círculos e cones.
    const glm::vec3 reference = std::abs(forward.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(forward, reference));
    const glm::vec3 up = glm::cross(right, forward);

    auto line = [&](const glm::vec3& a, const glm::vec3& b, float thickness) {
        const auto pa = Project(a), pb = Project(b);
        if (pa && pb) dl->AddLine(Add(origin, *pa), Add(origin, *pb), kHelperColor, thickness);
    };
    auto around = [](const glm::vec3& u, const glm::vec3& v, float angle) {
        return u * std::cos(angle) + v * std::sin(angle);
    };
    // Círculo no plano (u, v) em volta de `center`, em segmentos.
    auto circle = [&](const glm::vec3& center, const glm::vec3& u, const glm::vec3& v, float radius) {
        constexpr int kSegments = 48;
        constexpr float kStep = 6.2831853f / kSegments;
        for (int i = 0; i < kSegments; ++i) {
            line(center + around(u, v, i * kStep) * radius, center + around(u, v, (i + 1) * kStep) * radius, 1.5f);
        }
    };

    if (const LightComponent* light = e->Get<LightComponent>()) {
        switch (light->type) {
            case LightType::Directional:
                // Direção da luz: raios paralelos saindo do ícone.
                line(position, position + forward * 3.0f, 2.0f);
                for (int i = 0; i < 4; ++i) {
                    const glm::vec3 offset = around(right, up, i * 1.5707963f) * 0.4f;
                    line(position + offset, position + offset + forward * 2.0f, 1.0f);
                }
                break;
            case LightType::Point:
                // Alcance: três círculos (a esfera onde a luz chega a zero).
                circle(position, {1, 0, 0}, {0, 1, 0}, light->range);
                circle(position, {1, 0, 0}, {0, 0, 1}, light->range);
                circle(position, {0, 1, 0}, {0, 0, 1}, light->range);
                break;
            case LightType::Spot: {
                // Cone externo até o alcance.
                const float angle = glm::radians(glm::clamp(light->outerAngle, 1.0f, 89.0f));
                const glm::vec3 center = position + forward * (light->range * std::cos(angle));
                const float radius = light->range * std::sin(angle);
                circle(center, right, up, radius);
                for (int i = 0; i < 4; ++i) line(position, center + around(right, up, i * 1.5707963f) * radius, 1.5f);
                break;
            }
        }
    } else if (const CameraComponent* cam = e->Get<CameraComponent>()) {
        // Pirâmide de visão até uma distância curta (o far inteiro sairia da tela).
        const float distance = std::min(cam->farPlane, 3.0f);
        const float halfH = distance * std::tan(glm::radians(cam->fov) * 0.5f);
        const float halfW = halfH * m_framebuffer->AspectRatio();
        const glm::vec3 center = position + forward * distance;
        const glm::vec3 corners[4] = {center + right * halfW + up * halfH, center - right * halfW + up * halfH,
                                      center - right * halfW - up * halfH, center + right * halfW - up * halfH};
        for (int i = 0; i < 4; ++i) {
            line(position, corners[i], 1.5f);
            line(corners[i], corners[(i + 1) % 4], 1.5f);
        }
    }
}

void SceneViewport::DrawColliders(const EditorContext& ctx, ImDrawList* dl, const ImVec2& origin) {
    const Scene& scene = ctx.scene;
    for (const auto& e : scene.Entities()) {
        const ColliderComponent* collider = e->Get<ColliderComponent>();
        const CharacterControllerComponent* character = e->Get<CharacterControllerComponent>();
        if (!collider && !character) continue;
        const bool active = scene.IsActiveInHierarchy(e->id);
        const bool selected = e->id == ctx.selected;
        if (!active && !selected) continue;

        m_lines.clear();
        const glm::mat4 world = scene.WorldMatrix(e->id);
        if (collider) {
            const MeshRenderer* mr = collider->shape == ColliderShape::Mesh ? e->Get<MeshRenderer>() : nullptr;
            if (mr && mr->mesh) {
                const glm::vec3 lo = mr->mesh->BoundsMin(), hi = mr->mesh->BoundsMax();
                AppendColliderLines(*collider, world, m_lines, &lo, &hi);
            } else {
                AppendColliderLines(*collider, world, m_lines);
            }
        }
        if (character) AppendCharacterLines(*character, world, m_lines);

        const ImU32 color = selected ? kColliderSelectedColor : collider && collider->isTrigger ? kTriggerColor : kColliderColor;
        const float thickness = selected ? 2.0f : 1.0f;
        for (const GizmoLine& l : m_lines) {
            const auto pa = Project(l.a), pb = Project(l.b);
            if (pa && pb) dl->AddLine(Add(origin, *pa), Add(origin, *pb), color, thickness);
        }
    }
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
    ImGui::SameLine(0.0f, 14.0f);
    if (toggleButton("Colliders", m_showColliders)) m_showColliders = !m_showColliders;
    ImGui::SetItemTooltip("Mostrar as formas de colisão (verde), os triggers (azul) e os personagens");
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
