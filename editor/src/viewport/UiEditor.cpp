#include "viewport/UiEditor.h"

#include <motor/ui/UiComponents.h>
#include <motor/ui/UiDocument.h>

#include <cmath>

namespace editor {

using namespace motor;

namespace {
constexpr ImU32 kOutlineColor = IM_COL32(255, 255, 255, 70);
constexpr ImU32 kSelectedColor = IM_COL32(255, 158, 38, 255);  // a mesma cor da seleção 3D
} // namespace

Entity& CreateUiElement(EditorContext& ctx, UiElementKind kind, EntityId parent) {
    const char* names[] = {"Texto", "Imagem", "Botão", "Painel"};
    Entity& e = ctx.scene.CreateEntity(names[static_cast<int>(kind)], parent);
    UiRect& rect = e.Add<UiRect>();
    switch (kind) {
        case UiElementKind::Text:
            rect.size = {400.0f, 80.0f};
            e.Add<UiText>();
            break;
        case UiElementKind::Image:
            rect.size = {200.0f, 200.0f};
            e.Add<UiImage>();
            break;
        case UiElementKind::Button:
            rect.size = {320.0f, 90.0f};
            e.Add<UiButton>();
            e.Add<UiText>().text = "Botão";
            break;
        case UiElementKind::Panel:
            rect.size = {600.0f, 400.0f};
            e.Add<UiImage>().color = {0.05f, 0.06f, 0.08f, 0.7f};
            break;
    }
    ctx.selected = e.id;
    return e;
}

void DrawCreateUiMenuItems(EditorContext& ctx, EntityId parent) {
    if (ImGui::MenuItem("Texto")) CreateUiElement(ctx, UiElementKind::Text, parent);
    if (ImGui::MenuItem("Imagem")) CreateUiElement(ctx, UiElementKind::Image, parent);
    if (ImGui::MenuItem("Botão")) CreateUiElement(ctx, UiElementKind::Button, parent);
    if (ImGui::MenuItem("Painel")) CreateUiElement(ctx, UiElementKind::Panel, parent);
    ImGui::Separator();
    if (ImGui::MenuItem("Documento")) {
        // A tela inteira de um .mui: o arquivo se escolhe no Inspector.
        Entity& e = ctx.scene.CreateEntity("Documento de UI", parent);
        e.Add<UiDocument>();
        ctx.selected = e.id;
    }
}

void UiViewportTool::Draw(const EditorContext& ctx, ImDrawList* dl, const ImVec2& origin,
                          const std::vector<UiNode>& layout) const {
    for (const UiNode& node : layout) {
        const bool selected = node.entity == ctx.selected;
        const ImVec2 a{origin.x + node.box.min.x, origin.y + node.box.min.y};
        const ImVec2 b{origin.x + node.box.max.x, origin.y + node.box.max.y};
        dl->AddRect(a, b, selected ? kSelectedColor : kOutlineColor, 0.0f, 0, selected ? 2.0f : 1.0f);
        if (selected) {
            // Cantos marcados: dá para ver o retângulo mesmo com o elemento transparente.
            const float s = 5.0f;
            for (const ImVec2& c : {a, b, ImVec2(a.x, b.y), ImVec2(b.x, a.y)}) {
                dl->AddRectFilled(ImVec2(c.x - s * 0.5f, c.y - s * 0.5f), ImVec2(c.x + s * 0.5f, c.y + s * 0.5f),
                                  kSelectedColor);
            }
        }
    }
}

bool UiViewportTool::OnClick(EditorContext& ctx, const glm::vec2& mouse, const std::vector<UiNode>& layout) {
    const EntityId hit = UiHitTest(ctx.scene, layout, mouse, UiHitFilter::Any);
    if (hit == kNullEntity) return false;
    ctx.selected = hit;
    m_dragging = hit;
    m_remainder = glm::vec2(0.0f);
    return true;
}

void UiViewportTool::Update(EditorContext& ctx, const glm::vec2& viewportSize) {
    if (m_dragging == kNullEntity) return;
    Entity* e = ctx.scene.Find(m_dragging);
    // Documento de UI: o clique seleciona a entidade, mas os elementos se mudam no .mui (não há UiRect a arrastar).
    if (!e || !ImGui::IsMouseDown(ImGuiMouseButton_Left) || e->Has<motor::UiDocument>()) {
        m_dragging = kNullEntity;
        return;
    }
    const ImVec2 delta = ImGui::GetIO().MouseDelta;
    if (delta.x == 0.0f && delta.y == 0.0f) return;
    // Pixels do viewport -> pixels da tela de referência; a posição anda em pixels inteiros.
    m_remainder += glm::vec2(delta.x, delta.y) / UiScale(viewportSize);
    const glm::vec2 step{std::trunc(m_remainder.x), std::trunc(m_remainder.y)};
    if (step.x == 0.0f && step.y == 0.0f) return;
    m_remainder -= step;
    UiRect* rect = e->Get<UiRect>();
    if (!rect) rect = &e->Add<UiRect>();
    rect->position += step;
    if (rect->anchor == UiAnchor::Stretch) rect->size -= step;  // margens: direita/baixo andam junto
}

} // namespace editor
