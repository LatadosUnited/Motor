#include "motor/renderer/RenderWorld.h"

#include "motor/renderer/Pbr.h"

namespace motor {

RenderWorld::Id RenderWorld::Add(const RenderObject& object) {
    if (!object.mesh || !object.material || object.mesh->IsSkinned()) return kInvalid;
    Id id;
    if (!m_free.empty()) {
        id = m_free.back();
        m_free.pop_back();
    } else {
        id = static_cast<Id>(m_indexOf.size());
        m_indexOf.push_back(kInvalid);
    }
    const size_t index = m_entries.size();
    m_indexOf[id] = static_cast<uint32_t>(index);
    m_idOf.push_back(id);
    m_entries.emplace_back();
    m_objects.resize(2 * m_entries.size());
    Fill(m_entries[index], object);
    SetMatrices(index, object.model);
    return id;
}

void RenderWorld::Query(const Frustum& frustum, std::vector<Id>& out) const {
    const size_t first = out.size();
    QueryEntries(frustum, out);
    for (size_t i = first; i < out.size(); ++i) out[i] = m_idOf[out[i]];
}

void RenderWorld::QueryBox(const Aabb& box, std::vector<Id>& out) const {
    m_hits.clear();
    m_grid.QueryBox(box, m_hits);
    for (uint32_t cell : m_hits) out.push_back(m_idOf[m_entryOfCell[cell]]);
}

void RenderWorld::QueryRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                           std::vector<Id>& out, std::vector<float>* enter) const {
    m_hits.clear();
    if (enter) enter->clear();
    m_grid.QueryRay(origin, direction, maxDistance, m_hits, enter);
    for (uint32_t cell : m_hits) out.push_back(m_idOf[m_entryOfCell[cell]]);
}

void RenderWorld::QueryEntries(const Frustum& frustum, std::vector<uint32_t>& out) const {
    m_hits.clear();
    m_grid.QueryFrustum(frustum, m_hits);
    for (uint32_t cell : m_hits) out.push_back(m_entryOfCell[cell]);
}

bool RenderWorld::Update(Id id, const RenderObject& object) {
    if (!Contains(id) || !object.mesh || !object.material || object.mesh->IsSkinned()) return false;
    const size_t index = m_indexOf[id];
    Fill(m_entries[index], object);
    SetMatrices(index, object.model);
    return true;
}

bool RenderWorld::SetTransform(Id id, const glm::mat4& model) {
    if (!Contains(id)) return false;
    SetMatrices(m_indexOf[id], model);
    return true;
}

void RenderWorld::Remove(Id id) {
    if (!Contains(id)) return;
    const size_t index = m_indexOf[id];
    const size_t last = m_entries.size() - 1;
    m_grid.Remove(m_entries[index].cell);
    if (index != last) {
        m_entries[index] = std::move(m_entries[last]);
        m_objects[2 * index] = m_objects[2 * last];
        m_objects[2 * index + 1] = m_objects[2 * last + 1];
        m_idOf[index] = m_idOf[last];
        m_indexOf[m_idOf[index]] = static_cast<uint32_t>(index);
        m_entryOfCell[m_entries[index].cell] = static_cast<uint32_t>(index);
    }
    m_entries.pop_back();
    m_objects.resize(2 * m_entries.size());
    m_idOf.pop_back();
    m_indexOf[id] = kInvalid;
    m_free.push_back(id);
}

void RenderWorld::Clear() {
    m_entries.clear();
    m_objects.clear();
    m_indexOf.clear();
    m_idOf.clear();
    m_free.clear();
    m_grid.Clear();
    m_entryOfCell.clear();
    m_materials.Clear();
    m_shadows.Clear();
}

void RenderWorld::Fill(Entry& entry, const RenderObject& object) {
    const Material& material = *object.material;
    entry.mesh = object.mesh;
    entry.material = object.material;
    entry.materialId = m_materials.Id({MaterialParamsOf(material), material.doubleSided});
    entry.shadowId = m_shadows.Id(ShadowParamsOf(material));
    entry.shadows = object.shadows;
    entry.transparent = material.blend == BlendMode::Transparent;
}

void RenderWorld::SetMatrices(size_t index, const glm::mat4& model) {
    Entry& entry = m_entries[index];
    entry.bounds = TransformAabb({entry.mesh->BoundsMin(), entry.mesh->BoundsMax()}, model);
    if (entry.cell == SpatialGrid::kInvalid) {
        entry.cell = m_grid.Insert(entry.bounds);
        if (m_entryOfCell.size() <= entry.cell) m_entryOfCell.resize(entry.cell + 1);
    } else {
        m_grid.Update(entry.cell, entry.bounds);
    }
    m_entryOfCell[entry.cell] = static_cast<uint32_t>(index);
    m_objects[2 * index] = model;
    m_objects[2 * index + 1] = glm::mat4(NormalMatrix(model));
}

} // namespace motor
