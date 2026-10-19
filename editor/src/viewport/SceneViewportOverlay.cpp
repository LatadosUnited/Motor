// Viewport da cena: o que é desenhado por cima da imagem (ícones de luz e câmera, ajudantes do
// selecionado, colliders, gizmo e barra de ferramentas).

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
constexpr ImU32 kSelectionColorU32 = IM_COL32(255, 158, 38, 255);
constexpr ImU32 kHelperColor = IM_COL32(255, 230, 140, 200);
constexpr ImU32 kColliderColor = IM_COL32(110, 230, 120, 150);
constexpr ImU32 kColliderSelectedColor = IM_COL32(140, 255, 150, 255);
constexpr ImU32 kTriggerColor = IM_COL32(110, 200, 255, 150);  // triggers em azul, para diferenciar
constexpr float kIconRadius = 11.0f;      // raio do ícone em pixels

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
        if (ctx.IsSelected(icon.entity)) dl->AddCircle(c, kIconRadius + 3.0f, kSelectionColorU32, 0, 2.0f);

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
    if (m_prefs.colliders == ColliderOverlay::None) return;
    const Scene& scene = ctx.scene;
    auto drawEntity = [&](const Entity& e) {
        const ColliderComponent* collider = e.Get<ColliderComponent>();
        const CharacterControllerComponent* character = e.Get<CharacterControllerComponent>();
        if (!collider && !character) return;
        const bool selected = ctx.IsSelected(e.id);
        if (!ShouldDrawCollider(m_prefs.colliders, selected, scene.IsActiveInHierarchy(e.id))) return;

        m_lines.clear();
        const glm::mat4 world = scene.WorldMatrix(e.id);
        if (collider) {
            const MeshRenderer* mr = collider->shape == ColliderShape::Mesh ? e.Get<MeshRenderer>() : nullptr;
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
    };

    // Só os selecionados: nem percorre a cena (o custo não cresce com o número de objetos).
    if (m_prefs.colliders == ColliderOverlay::Selected) {
        for (EntityId id : ctx.selection) {
            if (const Entity* e = scene.Find(id)) drawEntity(*e);
        }
        return;
    }
    for (const auto& e : scene.Entities()) drawEntity(*e);
}

void SceneViewport::DrawGizmo(EditorContext& ctx, const ImVec2& origin) {
    const EntityId selected = ctx.selected;
    if (m_gizmoTool == GizmoTool::None || !ctx.scene.Find(selected)) return;
    // Elementos de UI ficam na tela, não no mundo: movem-se arrastando no viewport (ou pelo Inspector).
    if (IsUiEntity(*ctx.scene.Find(selected))) return;

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetGizmoSizeClipSpace(0.15f);  // padrão 0.1 fica pequeno no viewport
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(origin.x, origin.y, m_size.x, m_size.y);
    ImGuizmo::Enable(!m_looking);

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    float snapStep = m_prefs.snapMove;
    if (m_gizmoTool == GizmoTool::Rotate) {
        op = ImGuizmo::ROTATE;
        snapStep = m_prefs.snapRotate;
    } else if (m_gizmoTool == GizmoTool::Scale) {
        op = ImGuizmo::SCALE;
        snapStep = m_prefs.snapScale;
    }
    const ImGuizmo::MODE mode = m_gizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

    // Snap fixo pelo botão da barra, ou temporário segurando Ctrl.
    const bool snap = m_prefs.snap || ImGui::GetIO().KeyCtrl;
    const float snapValues[3] = {snapStep, snapStep, snapStep};

    const glm::mat4 view = m_camera.View();
    const glm::mat4 projection = m_camera.Projection();
    const glm::mat4 before = ctx.scene.WorldMatrix(selected);
    glm::mat4 world = before;

    if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), op, mode, glm::value_ptr(world),
                             nullptr, snap ? snapValues : nullptr)) {
        // O ImGuizmo encaixa o deslocamento a partir de onde o arraste começou; aqui a posição vai para a
        // grade (a mesma desenhada no viewport), e um objeto fora dela entra nela no primeiro passo.
        if (snap && op == ImGuizmo::TRANSLATE) {
            world[3] = glm::vec4(SnapToGrid(glm::vec3(before[3]), glm::vec3(world[3]), m_prefs.snapMove,
                                            m_prefs.gridHeight), 1.0f);
        }
        if (ctx.SelectionCount() <= 1) {
            ctx.scene.SetWorldMatrix(selected, world);
            return;
        }
        // Vários selecionados: a mesma mudança da principal em cada um dos de cima (girar e escalar em volta do
        // pivô da principal). Se a principal está dentro de outro selecionado, vai junto com ele.
        const glm::mat4 delta = world * glm::inverse(before);
        for (EntityId id : ctx.SelectionRoots()) {
            if (id == selected) {
                ctx.scene.SetWorldMatrix(id, world);
            } else if (!IsUiEntity(*ctx.scene.Find(id))) {
                ctx.scene.SetWorldMatrix(id, delta * ctx.scene.WorldMatrix(id));
                ctx.ForgetAngles(id);
            }
        }
    }
}

void SceneViewport::DrawSnapPopup() {
    if (!ImGui::BeginPopup("SnapOptions")) return;
    ImGui::SeparatorText("Encaixe");
    ImGui::Checkbox("Ligado", &m_prefs.snap);
    ImGui::SetNextItemWidth(110.0f);
    ImGui::DragFloat("Mover (m)", &m_prefs.snapMove, 0.01f, 0.01f, 100.0f, "%.3g", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("Passo do mover; é também o tamanho da célula da grade");
    ImGui::SetNextItemWidth(110.0f);
    ImGui::DragFloat("Girar (°)", &m_prefs.snapRotate, 0.5f, 0.1f, 180.0f, "%.3g", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(110.0f);
    ImGui::DragFloat("Escalar", &m_prefs.snapScale, 0.005f, 0.001f, 10.0f, "%.3g", ImGuiSliderFlags_AlwaysClamp);
    // Valores comuns num clique.
    ImGui::TextDisabled("Mover:");
    for (const float step : {0.1f, 0.25f, 0.5f, 1.0f, 2.0f}) {
        ImGui::SameLine();
        char label[16];
        std::snprintf(label, sizeof(label), "%g", step);
        if (ImGui::SmallButton(label)) m_prefs.snapMove = step;
    }
    ImGui::TextDisabled("Girar:");
    for (const float step : {5.0f, 15.0f, 45.0f, 90.0f}) {
        ImGui::SameLine();
        char label[16];
        std::snprintf(label, sizeof(label), "%g°", step);
        if (ImGui::SmallButton(label)) m_prefs.snapRotate = step;
    }
    ImGui::SeparatorText("Grade");
    ImGui::Checkbox("Visível", &m_prefs.grid);
    ImGui::SetNextItemWidth(110.0f);
    ImGui::DragFloat("Altura (Y)", &m_prefs.gridHeight, 0.05f, -10000.0f, 10000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("Plano da grade; o encaixe do Y conta a partir daqui");
    if (ImGui::SmallButton("Restaurar padrões")) {
        const ColliderOverlay colliders = m_prefs.colliders;
        m_prefs = ViewportPrefs{};
        m_prefs.colliders = colliders;
    }
    ImGui::EndPopup();
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
    if (toggleButton("Snap", m_prefs.snap)) m_prefs.snap = !m_prefs.snap;
    ImGui::SetItemTooltip("Encaixe na grade: %.3g m / %.3g° / %.3g (segure Ctrl para ativar temporariamente)",
                          m_prefs.snapMove, m_prefs.snapRotate, m_prefs.snapScale);
    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::ArrowButton("##snap_options", ImGuiDir_Down)) ImGui::OpenPopup("SnapOptions");
    ImGui::SetItemTooltip("Passos do encaixe e grade");
    DrawSnapPopup();
    ImGui::SameLine();
    if (toggleButton("Grade", m_prefs.grid)) m_prefs.grid = !m_prefs.grid;
    ImGui::SetItemTooltip("Mostrar a grade no chão (célula = passo do mover: %.3g m)", m_prefs.snapMove);
    ImGui::SameLine(0.0f, 14.0f);
    if (toggleButton(ColliderOverlayLabel(m_prefs.colliders), m_prefs.colliders != ColliderOverlay::None)) {
        m_prefs.colliders = NextColliderOverlay(m_prefs.colliders);
    }
    ImGui::SetItemTooltip("Formas de colisão (verde), triggers (azul) e personagens. Clique para alternar:\n"
                          "Selecionado (só o objeto selecionado), Todos ou Nenhum.");
    ImGui::SameLine();
    if (toggleButton("UI", m_showUi)) m_showUi = !m_showUi;
    ImGui::SetItemTooltip("Mostrar a interface do jogo (HUD) por cima da cena. Com ela ligada, um clique na UI a\n"
                          "seleciona e arrastar move o elemento; desligue para clicar nos objetos 3D por trás dela.");
    ImGui::PopStyleVar();
    ImGui::EndGroup();

    const ImVec2 groupMax = ImGui::GetItemRectMax();
    dl->ChannelsSetCurrent(0);
    dl->AddRectFilled(position, ImVec2(groupMax.x + 6.0f, groupMax.y + 5.0f), IM_COL32(0, 0, 0, 120), 4.0f);
    dl->ChannelsMerge();
}

} // namespace editor
