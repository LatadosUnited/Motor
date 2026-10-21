#include "EntityClipboard.h"

#include <algorithm>

namespace editor {

using motor::Entity;
using motor::EntityId;
using motor::kNullEntity;
using Pairs = std::vector<std::pair<EntityId, EntityId>>;

namespace {
EntityId Lookup(const Pairs& pairs, EntityId id) {
    const auto it = std::find_if(pairs.begin(), pairs.end(), [id](const auto& p) { return p.first == id; });
    return it != pairs.end() ? it->second : kNullEntity;
}
} // namespace

EntityId CopySubtree(const motor::Scene& from, EntityId id, motor::Scene& to, EntityId parent, Pairs& copies) {
    const Entity* source = from.Find(id);
    if (!source) return kNullEntity;
    Entity& clone = to.CreateEntity(source->name, parent);
    clone.active = source->active;
    clone.transform = source->transform;
    clone.components = source->components;
    motor::MarkSceneStructureChanged();
    const EntityId cloneId = clone.id;  // CreateEntity dos filhos pode realocar a lista
    copies.emplace_back(id, cloneId);
    for (EntityId child : from.Children(id)) CopySubtree(from, child, to, cloneId, copies);
    return cloneId;
}

void EntityClipboard::Clear() {
    m_scene = motor::Scene();
    m_roots.clear();
    m_originals.clear();
    m_sceneTag = 0;
}

void EntityClipboard::Copy(const motor::Scene& scene, const std::vector<EntityId>& roots, uint64_t sceneTag) {
    Clear();
    m_sceneTag = sceneTag;
    for (EntityId id : roots) {
        if (!scene.Find(id)) continue;
        const EntityId root = CopySubtree(scene, id, m_scene, kNullEntity, m_originals);
        m_scene.SetWorldMatrix(root, scene.WorldMatrix(id));  // sem pai aqui: o lugar no mundo vira o local
        m_roots.push_back(root);
    }
}

std::vector<EntityId> EntityClipboard::Paste(motor::Scene& scene, EntityId parent, uint64_t sceneTag) const {
    std::vector<EntityId> pasted;
    if (parent != kNullEntity && !scene.Find(parent)) parent = kNullEntity;
    Pairs copies;  // (na área de transferência, colada)
    for (EntityId root : m_roots) {
        const EntityId clone = CopySubtree(m_scene, root, scene, kNullEntity, copies);
        if (parent != kNullEntity) scene.SetParent(clone, parent, true);  // mantém o lugar no mundo
        pasted.push_back(clone);
    }
    // Referências: para dentro da cópia, a entidade colada correspondente; para fora, a mesma entidade se for
    // a mesma cena, senão nenhuma (o id seria de outra entidade, ou de nenhuma).
    const bool sameScene = sceneTag == m_sceneTag;
    for (const auto& [inClipboard, clone] : copies) {
        motor::RemapEntityRefs(*scene.Find(clone), [&](EntityId id) {
            if (id == kNullEntity) return id;
            const EntityId copied = Lookup(m_originals, id);
            if (copied != kNullEntity) {
                const EntityId target = Lookup(copies, copied);
                if (target != kNullEntity) return target;
            }
            return sameScene ? id : kNullEntity;
        });
    }
    return pasted;
}

} // namespace editor
