#include "motor/ui/UiLayout.h"

#include "motor/scene/Scene.h"

#include <glm/common.hpp>

#include <algorithm>
#include <unordered_map>

namespace motor {

glm::vec2 UiAnchorPoint(UiAnchor anchor) {
    switch (anchor) {
        case UiAnchor::TopLeft: return {0.0f, 0.0f};
        case UiAnchor::Top: return {0.5f, 0.0f};
        case UiAnchor::TopRight: return {1.0f, 0.0f};
        case UiAnchor::Left: return {0.0f, 0.5f};
        case UiAnchor::Center: return {0.5f, 0.5f};
        case UiAnchor::Right: return {1.0f, 0.5f};
        case UiAnchor::BottomLeft: return {0.0f, 1.0f};
        case UiAnchor::Bottom: return {0.5f, 1.0f};
        case UiAnchor::BottomRight: return {1.0f, 1.0f};
        case UiAnchor::Stretch: return {0.0f, 0.0f};
    }
    return {0.5f, 0.5f};
}

UiBox ResolveUiRect(const UiRect& rect, const UiBox& parent, float scale) {
    if (rect.anchor == UiAnchor::Stretch) {
        UiBox box{parent.min + rect.position * scale, parent.max - rect.size * scale};
        box.max = glm::max(box.max, box.min);  // margens maiores que o pai: retângulo vazio, não invertido
        return box;
    }
    const glm::vec2 anchor = UiAnchorPoint(rect.anchor);
    const glm::vec2 size = glm::max(rect.size, glm::vec2(0.0f)) * scale;
    const glm::vec2 point = parent.min + anchor * parent.Size() + rect.position * scale;
    const glm::vec2 min = point - anchor * size;
    return {min, min + size};
}

namespace {
const UiRect kDefaultRect{};

struct LayoutBuilder {
    const Scene& scene;
    float scale;
    std::unordered_map<EntityId, std::vector<const Entity*>> children;  // na ordem da cena
    std::vector<UiNode> nodes;

    void Visit(EntityId parent, const UiBox& parentBox, int depth) {
        const auto it = children.find(parent);
        if (it == children.end()) return;
        // Irmãos por ordem (estável: empate fica na ordem da cena). Sem UiRect = ordem 0.
        std::vector<const Entity*> list = it->second;
        auto orderOf = [](const Entity* e) {
            const UiRect* r = e->Get<UiRect>();
            return r ? r->order : 0;
        };
        std::stable_sort(list.begin(), list.end(), [&](const Entity* a, const Entity* b) { return orderOf(a) < orderOf(b); });

        for (const Entity* e : list) {
            if (!e->active) continue;  // inativa esconde a sub-árvore (como na cena)
            if (!IsUiEntity(*e)) {
                Visit(e->id, parentBox, depth);  // entidade comum no meio: os filhos usam o retângulo de cima
                continue;
            }
            const UiRect* rect = e->Get<UiRect>();
            if (rect && !rect->visible) continue;
            const UiBox box = ResolveUiRect(rect ? *rect : kDefaultRect, parentBox, scale);
            nodes.push_back({e->id, box, depth});
            Visit(e->id, box, depth + 1);
        }
    }
};
} // namespace

std::vector<UiNode> ComputeUiLayout(const Scene& scene, const glm::vec2& screenSize) {
    LayoutBuilder builder{scene, UiScale(screenSize), {}, {}};
    for (const auto& e : scene.Entities()) builder.children[e->parent].push_back(e.get());
    builder.Visit(kNullEntity, UiBox{{0.0f, 0.0f}, screenSize}, 0);
    return std::move(builder.nodes);
}

EntityId UiHitTest(const Scene& scene, const std::vector<UiNode>& layout, const glm::vec2& point, UiHitFilter filter) {
    for (auto it = layout.rbegin(); it != layout.rend(); ++it) {
        if (!it->box.Contains(point)) continue;
        const Entity* e = scene.Find(it->entity);
        if (!e) continue;
        if (filter == UiHitFilter::Any || e->Has<UiImage>() || e->Has<UiButton>()) return it->entity;
    }
    return kNullEntity;
}

} // namespace motor
