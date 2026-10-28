#include "UiFlexLayout.h"

#include "motor/ui/Font.h"
#include "motor/ui/UiDocument.h"

#include <glm/common.hpp>

#include <algorithm>

namespace motor {

namespace {

// Como um elemento arruma os filhos (a raiz do documento usa o padrão: coluna, do começo, esticando).
struct Container {
    UiDirection direction = UiDirection::Column;
    UiJustify justify = UiJustify::Start;
    UiAlign alignItems = UiAlign::Stretch;
    float gap = 0.0f;
};

Container ContainerOf(const UiElement& e) { return {e.direction, e.justify, e.alignItems, e.gap}; }

bool InFlow(const UiElement& e) { return e.visible && e.placement == UiPlacement::Flow; }

// Tamanho em pixels de verdade; < 0 = automático (ou porcentagem de um pai sem tamanho definido).
float Resolve(const UiLength& length, float base, float scale) {
    switch (length.unit) {
        case UiUnit::Auto: return -1.0f;
        case UiUnit::Px: return std::max(0.0f, length.value * scale);
        case UiUnit::Percent: return base >= 0.0f ? std::max(0.0f, base * length.value / 100.0f) : -1.0f;
    }
    return -1.0f;
}

float Horizontal(const UiEdges& e, float scale) { return (e.left + e.right) * scale; }
float Vertical(const UiEdges& e, float scale) { return (e.top + e.bottom) * scale; }

UiBox Intersect(const UiBox& a, const UiBox& b) {
    UiBox box{glm::max(a.min, b.min), glm::min(a.max, b.max)};
    box.max = glm::max(box.max, box.min);
    return box;
}

glm::vec2 MeasureBox(const UiElement& e, float scale, float parentW, float parentH, float availW, float forcedW);

// Tamanho do conteúdo (sem o padding): o texto e os filhos no fluxo. availW: largura para a quebra do texto (< 0 =
// sem limite).
glm::vec2 ContentSize(const UiElement& e, float scale, float availW) {
    glm::vec2 size{0.0f};
    if (e.HasText()) {
        if (const FontData* font = e.Font()) {
            size = MeasureText(*font, e.text, e.fontSize * scale, e.wrap && availW > 0.0f ? availW : 0.0f);
        }
    }
    const bool row = e.direction == UiDirection::Row;
    float main = 0.0f, cross = 0.0f;
    int count = 0;
    for (const UiElement& c : e.children) {
        if (!InFlow(c)) continue;
        const float childAvail = row || availW < 0.0f ? -1.0f : availW - Horizontal(c.margin, scale);
        const glm::vec2 outer = MeasureBox(c, scale, -1.0f, -1.0f, childAvail, -1.0f) +
                                glm::vec2(Horizontal(c.margin, scale), Vertical(c.margin, scale));
        main += row ? outer.x : outer.y;
        cross = std::max(cross, row ? outer.y : outer.x);
        ++count;
    }
    if (count > 1) main += e.gap * scale * static_cast<float>(count - 1);
    return glm::max(size, row ? glm::vec2(main, cross) : glm::vec2(cross, main));
}

// Caixa do elemento (com o padding). width/height definidos valem; automáticos vêm do conteúdo. forcedW >= 0 fixa a
// largura (esticado ou já repartido pelo flex): a altura sai do texto quebrado nela.
glm::vec2 MeasureBox(const UiElement& e, float scale, float parentW, float parentH, float availW, float forcedW) {
    const float padH = Horizontal(e.padding, scale), padV = Vertical(e.padding, scale);
    float w = forcedW >= 0.0f ? forcedW : Resolve(e.width, parentW, scale);
    float h = Resolve(e.height, parentH, scale);
    if (w < 0.0f) w = ContentSize(e, scale, availW >= 0.0f ? availW - padH : -1.0f).x + padH;
    if (h < 0.0f) h = ContentSize(e, scale, std::max(0.0f, w - padH)).y + padV;
    return {w, h};
}

struct Layout {
    EntityId owner;
    float scale;
    std::vector<UiNode>& out;

    // Nó do elemento e, depois, os filhos dentro do conteúdo dele.
    void Place(UiElement& e, const UiBox& box, int depth, const UiBox* clip) {
        UiBox content{box.min + glm::vec2(e.padding.left, e.padding.top) * scale,
                      box.max - glm::vec2(e.padding.right, e.padding.bottom) * scale};
        content.max = glm::max(content.max, content.min);
        out.push_back({owner, box, depth, &e, content, clip ? *clip : UiBox{}, clip != nullptr});
        UiBox childClip;
        const UiBox* inner = clip;
        if (e.overflow != UiOverflow::Visible) {
            childClip = clip ? Intersect(*clip, box) : box;
            inner = &childClip;
        }
        Children(e.children, ContainerOf(e), content, box, depth + 1, inner,
                 e.overflow == UiOverflow::Scroll ? &e : nullptr);
    }

    void Children(std::vector<UiElement>& children, const Container& c, const UiBox& content, const UiBox& outer,
                  int depth, const UiBox* clip, UiElement* scroller) {
        const bool row = c.direction == UiDirection::Row;
        const glm::vec2 size = content.Size();
        const float mainSize = row ? size.x : size.y;
        const float crossSize = row ? size.y : size.x;

        struct Item {
            size_t index;
            float main, cross;
            float mainStart, mainEnd, crossStart, crossEnd;
            UiAlign align;
            bool crossMeasured;  // a altura veio do conteúdo (numa linha, refeita depois da largura final)
        };
        std::vector<Item> items;
        std::vector<UiBox> boxes(children.size());
        float used = 0.0f, totalGrow = 0.0f, totalShrink = 0.0f;
        for (size_t i = 0; i < children.size(); ++i) {
            const UiElement& child = children[i];
            if (!InFlow(child)) continue;
            Item item{};
            item.index = i;
            const UiEdges& m = child.margin;
            item.mainStart = (row ? m.left : m.top) * scale;
            item.mainEnd = (row ? m.right : m.bottom) * scale;
            item.crossStart = (row ? m.top : m.left) * scale;
            item.crossEnd = (row ? m.bottom : m.right) * scale;
            item.align = child.alignSelf == UiAlign::Auto ? c.alignItems : child.alignSelf;
            const float mainSpecified = Resolve(row ? child.width : child.height, mainSize, scale);
            float cross = Resolve(row ? child.height : child.width, crossSize, scale);
            if (cross < 0.0f && item.align == UiAlign::Stretch) {
                cross = std::max(0.0f, crossSize - item.crossStart - item.crossEnd);
            }
            if (row) {
                const float avail = mainSpecified >= 0.0f ? mainSpecified : mainSize - item.mainStart - item.mainEnd;
                const glm::vec2 b = MeasureBox(child, scale, mainSize, crossSize, avail, mainSpecified);
                item.main = mainSpecified >= 0.0f ? mainSpecified : b.x;
                item.crossMeasured = cross < 0.0f;
                item.cross = cross >= 0.0f ? cross : b.y;
            } else {
                const float avail = cross >= 0.0f ? cross : crossSize - item.crossStart - item.crossEnd;
                const glm::vec2 b = MeasureBox(child, scale, crossSize, mainSize, avail, cross);
                item.main = mainSpecified >= 0.0f ? mainSpecified : b.y;
                item.cross = cross >= 0.0f ? cross : b.x;
            }
            used += item.main + item.mainStart + item.mainEnd;
            totalGrow += std::max(0.0f, child.grow);
            totalShrink += std::max(0.0f, child.shrink) * item.main;
            items.push_back(item);
        }
        const size_t n = items.size();
        const float gap = c.gap * scale;
        if (n > 1) used += gap * static_cast<float>(n - 1);
        float free = mainSize - used;

        // Sobra para quem cresce; falta tirada de quem encolhe, na proporção do tamanho.
        if (free > 0.0f && totalGrow > 0.0f) {
            for (Item& item : items) item.main += free * std::max(0.0f, children[item.index].grow) / totalGrow;
            free = 0.0f;
        } else if (free < 0.0f && totalShrink > 0.0f && !(scroller && !row)) {  // numa lista que rola, sobra rolagem
            for (Item& item : items) {
                const float weight = std::max(0.0f, children[item.index].shrink) * item.main;
                item.main = std::max(0.0f, item.main + free * weight / totalShrink);
            }
            free = 0.0f;
        }
        if (row) {
            // Largura final mudou: a altura de um texto que quebra muda junto.
            for (Item& item : items) {
                if (item.crossMeasured) {
                    item.cross = MeasureBox(children[item.index], scale, mainSize, crossSize, item.main, item.main).y;
                }
            }
        }

        float cursor = 0.0f, between = gap;
        if (free > 0.0f && n > 0) {
            switch (c.justify) {
                case UiJustify::Start: break;
                case UiJustify::Center: cursor = free * 0.5f; break;
                case UiJustify::End: cursor = free; break;
                case UiJustify::SpaceBetween:
                    if (n > 1) between += free / static_cast<float>(n - 1);
                    else cursor = 0.0f;
                    break;
                case UiJustify::SpaceAround:
                    between += free / static_cast<float>(n);
                    cursor = free / static_cast<float>(n) * 0.5f;
                    break;
            }
        }
        for (const Item& item : items) {
            const float mainPos = cursor + item.mainStart;
            float crossPos = item.crossStart;
            if (item.align == UiAlign::Center) {
                crossPos = item.crossStart + (crossSize - item.crossStart - item.crossEnd - item.cross) * 0.5f;
            } else if (item.align == UiAlign::End) {
                crossPos = crossSize - item.crossEnd - item.cross;
            }
            const glm::vec2 offset = row ? glm::vec2(mainPos, crossPos) : glm::vec2(crossPos, mainPos);
            const glm::vec2 extent = row ? glm::vec2(item.main, item.cross) : glm::vec2(item.cross, item.main);
            boxes[item.index] = {content.min + offset, content.min + offset + extent};
            cursor = mainPos + item.main + item.mainEnd + between;
        }

        // Fora do fluxo: âncora no pai inteiro, como o UiRect; tamanho automático vem do conteúdo.
        for (size_t i = 0; i < children.size(); ++i) {
            const UiElement& child = children[i];
            if (!child.visible || child.placement != UiPlacement::Absolute) continue;
            const glm::vec2 parent = outer.Size();
            const glm::vec2 b = MeasureBox(child, scale, parent.x, parent.y, parent.x, -1.0f);
            UiRect rect;
            rect.anchor = child.anchor;
            rect.position = child.position;
            rect.size = b / scale;
            if (child.anchor == UiAnchor::Stretch) {  // margens: size são as da direita e de baixo
                rect.size = {Resolve(child.width, parent.x, 1.0f) >= 0.0f ? child.width.value : 0.0f,
                             Resolve(child.height, parent.y, 1.0f) >= 0.0f ? child.height.value : 0.0f};
            }
            boxes[i] = ResolveUiRect(rect, outer, scale);
        }

        // Rolagem: o quanto o conteúdo passa da caixa, e tudo sobe pelo scroll.
        if (scroller) {
            float bottom = content.min.y;
            for (size_t i = 0; i < children.size(); ++i) {
                if (children[i].visible) bottom = std::max(bottom, boxes[i].max.y + children[i].margin.bottom * scale);
            }
            scroller->scrollMax = std::max(0.0f, bottom - content.max.y) / scale;
            scroller->scroll = std::clamp(scroller->scroll, 0.0f, scroller->scrollMax);
            const glm::vec2 shift{0.0f, -scroller->scroll * scale};
            for (UiBox& b : boxes) {
                b.min += shift;
                b.max += shift;
            }
        }

        for (size_t i = 0; i < children.size(); ++i) {
            if (children[i].visible) Place(children[i], boxes[i], depth, clip);
        }
    }
};

} // namespace

void LayoutUiDocument(EntityId owner, std::vector<UiElement>& elements, const UiBox& box, float scale, int depth,
                      std::vector<UiNode>& out) {
    Layout layout{owner, scale, out};
    layout.Children(elements, Container{}, box, box, depth, nullptr, nullptr);
}

} // namespace motor
