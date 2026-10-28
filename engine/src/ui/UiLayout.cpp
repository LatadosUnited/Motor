#include "motor/ui/UiLayout.h"

#include "motor/renderer/Camera.h"
#include "motor/scene/Scene.h"
#include "motor/ui/UiDocument.h"

#include "UiFlexLayout.h"

#include <glm/common.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

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
    glm::vec2 screen{0.0f};
    std::unordered_map<EntityId, std::vector<const Entity*>> children;  // na ordem da cena
    std::vector<UiNode> nodes;
    int cameraState = 0;  // 0: ainda não procurou; 1: tem câmera; -1: a cena não tem câmera principal
    glm::mat4 viewProjection{1.0f};

    // Ponto do mundo na tela, pela câmera principal (a do jogo). false atrás da câmera, além do far ou sem câmera.
    bool Project(const glm::vec3& world, glm::vec2& out) {
        if (cameraState == 0) {
            Camera camera;
            camera.aspect = screen.y > 0.0f ? screen.x / screen.y : 1.0f;
            cameraState = scene.ApplyCamera(scene.FindMainCamera(), camera) ? 1 : -1;
            if (cameraState > 0) viewProjection = camera.Projection() * camera.View();
        }
        if (cameraState < 0) return false;
        const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
        if (clip.w <= 1e-4f) return false;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z > 1.0f) return false;
        out = {(ndc.x * 0.5f + 0.5f) * screen.x, (0.5f - ndc.y * 0.5f) * screen.y};
        return true;
    }

    // Elementos com `follow` (Absolute) dos nós a partir de `start`: a âncora vai para o ponto projetado da entidade
    // (mais `position`); atrás da câmera (ou sem a entidade), o elemento e os filhos saem da lista. Feito depois do
    // layout (que fica no cache): só as caixas destes andam a cada quadro.
    void ApplyFollow(size_t start) {
        for (size_t i = start; i < nodes.size(); ++i) {
            const UiElement* e = nodes[i].element;
            if (!e || e->follow.empty() || e->placement != UiPlacement::Absolute) continue;
            size_t end = i + 1;  // os descendentes vêm logo depois (pai antes dos filhos)
            while (end < nodes.size() && nodes[end].parent >= static_cast<int>(i)) ++end;
            // FindByName só monta o índice de nomes da cena (cache): não muda a cena.
            const Entity* target = const_cast<Scene&>(scene).FindByName(e->follow);
            glm::vec2 point{0.0f};
            if (!target || !Project(glm::vec3(scene.WorldMatrix(target->id)[3]) + e->followOffset, point)) {
                const int removed = static_cast<int>(end - i);
                nodes.erase(nodes.begin() + static_cast<std::ptrdiff_t>(i), nodes.begin() + static_cast<std::ptrdiff_t>(end));
                for (size_t k = i; k < nodes.size(); ++k) {
                    if (nodes[k].parent >= static_cast<int>(end)) nodes[k].parent -= removed;
                }
                --i;
                continue;
            }
            const UiBox box = nodes[i].box;
            const glm::vec2 anchor = UiAnchorPoint(e->anchor == UiAnchor::Stretch ? UiAnchor::Center : e->anchor);
            const glm::vec2 delta = point + e->position * scale - anchor * box.Size() - box.min;
            const UiBox ownClip = nodes[i].clip;
            for (size_t k = i; k < end; ++k) {
                UiNode& n = nodes[k];
                n.box = {n.box.min + delta, n.box.max + delta};
                n.content = {n.content.min + delta, n.content.max + delta};
                if (k > i && n.clipped && !(n.clip == ownClip)) n.clip = {n.clip.min + delta, n.clip.max + delta};
            }
        }
    }

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
                if (UiDocumentData* tree = document->Tree()) {
                    UpdateUiStyles(*tree);  // estado de um botão mudou: a cascata roda antes do layout
                    const size_t start = nodes.size();
                    LayoutUiDocument(e->id, *tree, box, scale, depth, nodes);
                    ApplyFollow(start);
                }
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
    LayoutBuilder builder{scene, UiScale(screenSize), screenSize, {}, {}};
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
