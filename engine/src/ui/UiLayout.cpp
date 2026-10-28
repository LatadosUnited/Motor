#include "motor/ui/UiLayout.h"

#include "motor/scene/Scene.h"
#include "motor/ui/UiDocument.h"

#include "UiFlexLayout.h"

#include <glm/common.hpp>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

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
            if (const UiRect* r = e->Get<UiRect>()) return r->order;
            const UiDocument* d = e->Get<UiDocument>();
            return d ? d->order : 0;
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
            if (const UiDocument* document = e->Get<UiDocument>()) {
                // O documento ocupa o pai (ou o próprio UiRect, se a entidade tiver um).
                if (!document->visible) continue;
                const UiBox box = rect ? ResolveUiRect(*rect, parentBox, scale) : parentBox;
                if (UiDocumentData* tree = document->Tree()) LayoutUiDocument(e->id, tree->elements, box, scale, depth, nodes);
                Visit(e->id, box, depth + 1);
                continue;
            }
            const UiBox box = ResolveUiRect(rect ? *rect : kDefaultRect, parentBox, scale);
            nodes.push_back({e->id, box, depth, nullptr, box, UiBox{}, false});
            Visit(e->id, box, depth + 1);
        }
    }
};
} // namespace

std::vector<UiNode> ComputeUiLayout(const Scene& scene, const glm::vec2& screenSize) {
    LayoutBuilder builder{scene, UiScale(screenSize), {}, {}};
    // Só entram as entidades de UI e as que estão acima delas: as outras não geram nó nenhum, e numa cena de 10 000
    // objetos sem UI montar e ordenar a árvore inteira custava ~7 ms por chamada (Fase 22, medido com o Tracy).
    // As entidades de UI vêm das listas por tipo da cena (sem percorrer a cena); só a ordem dos filhos, abaixo,
    // percorre a cena, e só quando há UI.
    std::unordered_set<EntityId> relevant;
    auto addWithAncestors = [&](const std::vector<EntityId>& ids) {
        for (EntityId id : ids) {
            for (const Entity* up = scene.Find(id); up && relevant.insert(up->id).second; up = scene.Find(up->parent)) {
            }
        }
    };
    addWithAncestors(scene.EntitiesWith<UiRect>());
    addWithAncestors(scene.EntitiesWith<UiImage>());
    addWithAncestors(scene.EntitiesWith<UiText>());
    addWithAncestors(scene.EntitiesWith<UiButton>());
    addWithAncestors(scene.EntitiesWith<UiDocument>());
    if (relevant.empty()) return {};
    for (const auto& e : scene.Entities()) {
        if (relevant.contains(e->id)) builder.children[e->parent].push_back(e.get());
    }
    builder.Visit(kNullEntity, UiBox{{0.0f, 0.0f}, screenSize}, 0);
    return std::move(builder.nodes);
}

const UiNode* UiHitTestNode(const Scene& scene, const std::vector<UiNode>& layout, const glm::vec2& point,
                            UiHitFilter filter) {
    for (auto it = layout.rbegin(); it != layout.rend(); ++it) {
        if (!it->box.Contains(point) || (it->clipped && !it->clip.Contains(point))) continue;  // recortado: não pega
        if (filter == UiHitFilter::Any) return &*it;
        if (it->element) {
            if (it->element->BlocksPointer()) return &*it;
            continue;
        }
        const Entity* e = scene.Find(it->entity);
        if (e && (e->Has<UiImage>() || e->Has<UiButton>())) return &*it;
    }
    return nullptr;
}

EntityId UiHitTest(const Scene& scene, const std::vector<UiNode>& layout, const glm::vec2& point, UiHitFilter filter) {
    const UiNode* node = UiHitTestNode(scene, layout, point, filter);
    return node ? node->entity : kNullEntity;
}

} // namespace motor
