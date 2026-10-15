#include "motor/scene/Scene.h"

#include "motor/renderer/Camera.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <glm/common.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace motor {

namespace {
bool SameEntity(const Entity& a, const Entity& b) {
    if (a.id != b.id || a.name != b.name || a.active != b.active || a.parent != b.parent) return false;
    const Transform &ta = a.transform, &tb = b.transform;
    if (ta.position != tb.position || ta.rotation != tb.rotation || ta.scale != tb.scale) return false;
    if (a.components.size() != b.components.size()) return false;
    for (size_t i = 0; i < a.components.size(); ++i) {
        const ComponentPtr &ca = a.components[i], &cb = b.components[i];
        if (&ca.Type() != &cb.Type() || !ComponentsEqual(ca.Type(), ca.Data(), cb.Data())) return false;
    }
    return true;
}
} // namespace

// --- Entity -----------------------------------------------------------------------

void* Entity::Get(const ComponentType& type) {
    for (ComponentPtr& c : components) {
        if (&c.Type() == &type) return c.Data();
    }
    return nullptr;
}

const void* Entity::Get(const ComponentType& type) const {
    return const_cast<Entity*>(this)->Get(type);
}

void* Entity::Add(const ComponentType& type) {
    if (void* existing = Get(type)) return existing;
    return components.emplace_back(type).Data();
}

void Entity::Remove(const ComponentType& type) {
    std::erase_if(components, [&](const ComponentPtr& c) { return &c.Type() == &type; });
}

// --- Scene ------------------------------------------------------------------------

Scene::Scene(const Scene& other) : light(other.light), environment(other.environment), m_nextId(other.m_nextId) {
    m_entities.reserve(other.m_entities.size());
    for (const auto& e : other.m_entities) {
        m_entities.push_back(std::make_unique<Entity>(*e));
        m_byId[e->id] = m_entities.back().get();
    }
}

Scene& Scene::operator=(const Scene& other) {
    if (this != &other) *this = Scene(other);  // copia e move
    return *this;
}

bool Scene::operator==(const Scene& other) const {
    if (m_entities.size() != other.m_entities.size()) return false;
    const DirectionalLight &la = light, &lb = other.light;
    if (la.direction != lb.direction || la.color != lb.color || la.intensity != lb.intensity ||
        la.ambient != lb.ambient) {
        return false;
    }
    if (environment.skyColor != other.environment.skyColor || environment.fogDensity != other.environment.fogDensity) {
        return false;
    }
    for (size_t i = 0; i < m_entities.size(); ++i) {
        if (!SameEntity(*m_entities[i], *other.m_entities[i])) return false;
    }
    return true;
}

Entity& Scene::CreateEntity(std::string name, EntityId parent) {
    auto entity = std::make_unique<Entity>();
    entity->id = m_nextId++;
    entity->name = std::move(name);
    entity->parent = Find(parent) ? parent : kNullEntity;

    Entity& ref = *entity;
    m_byId[ref.id] = &ref;
    m_entities.push_back(std::move(entity));
    return ref;
}

void Scene::DestroyEntity(EntityId id) {
    if (!Find(id)) return;

    std::unordered_set<EntityId> doomed{id};
    for (const auto& e : m_entities) {
        if (IsDescendantOf(e->id, id)) doomed.insert(e->id);
    }
    for (EntityId d : doomed) m_byId.erase(d);
    std::erase_if(m_entities, [&](const auto& e) { return doomed.contains(e->id); });
}

EntityId Scene::Duplicate(EntityId id) {
    const Entity* source = Find(id);
    if (!source) return kNullEntity;
    const Entity copy = *source;  // cópia: CreateEntity pode realocar o vetor
    const EntityId clone = CloneRecursive(copy, copy.parent);
    Find(clone)->name = copy.name + " (cópia)";
    return clone;
}

EntityId Scene::CloneRecursive(const Entity& source, EntityId newParent) {
    const std::vector<EntityId> children = Children(source.id);

    Entity& clone = CreateEntity(source.name, newParent);
    clone.active = source.active;
    clone.transform = source.transform;
    clone.components = source.components;
    const EntityId cloneId = clone.id;

    for (EntityId child : children) {
        const Entity childCopy = *Find(child);
        CloneRecursive(childCopy, cloneId);
    }
    return cloneId;
}

Entity* Scene::Find(EntityId id) {
    auto it = m_byId.find(id);
    return it != m_byId.end() ? it->second : nullptr;
}

const Entity* Scene::Find(EntityId id) const {
    auto it = m_byId.find(id);
    return it != m_byId.end() ? it->second : nullptr;
}

std::vector<EntityId> Scene::Children(EntityId parent) const {
    std::vector<EntityId> result;
    for (const auto& e : m_entities) {
        if (e->parent == parent) result.push_back(e->id);
    }
    return result;
}

bool Scene::IsDescendantOf(EntityId id, EntityId ancestor) const {
    const Entity* e = Find(id);
    while (e && e->parent != kNullEntity) {
        if (e->parent == ancestor) return true;
        e = Find(e->parent);
    }
    return false;
}

bool Scene::IsActiveInHierarchy(EntityId id) const {
    for (const Entity* e = Find(id); e; e = Find(e->parent)) {
        if (!e->active) return false;
    }
    return true;
}

bool Scene::SetParent(EntityId id, EntityId newParent, bool keepWorldTransform) {
    Entity* e = Find(id);
    if (!e) return false;
    if (newParent != kNullEntity && (!Find(newParent) || newParent == id || IsDescendantOf(newParent, id))) {
        return false;
    }
    if (e->parent == newParent) return true;

    const glm::mat4 world = WorldMatrix(id);
    e->parent = newParent;
    if (keepWorldTransform) SetWorldMatrix(id, world);
    return true;
}

void Scene::SetWorldMatrix(EntityId id, const glm::mat4& world) {
    Entity* e = Find(id);
    if (!e) return;

    const glm::mat4 parentWorld = e->parent != kNullEntity ? WorldMatrix(e->parent) : glm::mat4(1.0f);
    const glm::mat4 local = glm::inverse(parentWorld) * world;

    glm::vec3 position, scale, skew;
    glm::quat rotation;
    glm::vec4 perspective;
    if (!glm::decompose(local, scale, rotation, position, skew, perspective)) return;  // matriz degenerada

    e->transform.position = position;
    e->transform.rotation = glm::normalize(rotation);
    e->transform.scale = scale;
}

glm::mat4 Scene::WorldMatrix(EntityId id) const {
    glm::mat4 m(1.0f);
    for (const Entity* e = Find(id); e; e = Find(e->parent)) {
        m = e->transform.Matrix() * m;
    }
    return m;
}

void Scene::Render(Renderer& renderer, const Camera& camera, const Framebuffer* target) const {
    renderer.BeginScene(camera, light, environment, target);
    SubmitTo(renderer);
    renderer.EndScene();
}

void Scene::SubmitTo(Renderer& renderer) const {
    for (const auto& e : m_entities) {
        const MeshRenderer* mr = e->Get<MeshRenderer>();
        if (!mr || !mr->mesh || !IsActiveInHierarchy(e->id)) continue;
        renderer.Submit(*mr->mesh, mr->material, WorldMatrix(e->id));
    }
}

namespace {
// Teste de raio contra AABB (método das "slabs"). Retorna o t de entrada, se houver.
std::optional<float> RayAabb(const glm::vec3& o, const glm::vec3& d, const glm::vec3& bmin, const glm::vec3& bmax) {
    float tmin = 0.0f;
    float tmax = std::numeric_limits<float>::max();
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-8f) {
            if (o[i] < bmin[i] || o[i] > bmax[i]) return std::nullopt;
            continue;
        }
        const float inv = 1.0f / d[i];
        float t1 = (bmin[i] - o[i]) * inv;
        float t2 = (bmax[i] - o[i]) * inv;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return std::nullopt;
    }
    return tmin;
}
} // namespace

std::optional<RaycastHit> Scene::Raycast(const glm::vec3& origin, const glm::vec3& direction) const {
    std::optional<RaycastHit> best;
    for (const auto& e : m_entities) {
        const MeshRenderer* mr = e->Get<MeshRenderer>();
        if (!mr || !mr->mesh || !IsActiveInHierarchy(e->id)) continue;

        // Leva o raio para o espaço local; como a transformação é linear, o t vale nos dois espaços.
        const glm::mat4 inv = glm::inverse(WorldMatrix(e->id));
        const glm::vec3 o = glm::vec3(inv * glm::vec4(origin, 1.0f));
        const glm::vec3 d = glm::vec3(inv * glm::vec4(direction, 0.0f));

        const Mesh& mesh = *mr->mesh;
        if (auto t = RayAabb(o, d, mesh.BoundsMin(), mesh.BoundsMax())) {
            if (!best || *t < best->distance) best = RaycastHit{e->id, *t};
        }
    }
    return best;
}

} // namespace motor
