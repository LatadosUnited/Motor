#include "motor/scene/Scene.h"
#include "motor/scene/EntityRef.h"

#include "motor/renderer/Camera.h"
#include "motor/renderer/Mesh.h"
#include "motor/renderer/Renderer.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
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

Scene::Scene(const Scene& other) : environment(other.environment), m_nextId(other.m_nextId) {
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
    if (environment != other.environment) return false;
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
    std::vector<std::pair<EntityId, EntityId>> copies;
    const EntityId clone = CloneRecursive(copy, copy.parent, copies);
    Find(clone)->name = copy.name + " (cópia)";
    // Referências para dentro da sub-árvore copiada apontam para as cópias (ex.: o script do inimigo e a arma filha).
    for (const auto& [original, duplicate] : copies) {
        RemapEntityRefs(*Find(duplicate), [&](EntityId id) {
            for (const auto& [from, to] : copies) {
                if (from == id) return to;
            }
            return id;
        });
    }
    return clone;
}

EntityId Scene::CloneRecursive(const Entity& source, EntityId newParent,
                               std::vector<std::pair<EntityId, EntityId>>& copies) {
    const std::vector<EntityId> children = Children(source.id);

    Entity& clone = CreateEntity(source.name, newParent);
    clone.active = source.active;
    clone.transform = source.transform;
    clone.components = source.components;
    const EntityId cloneId = clone.id;
    copies.emplace_back(source.id, cloneId);

    for (EntityId child : children) {
        const Entity childCopy = *Find(child);
        CloneRecursive(childCopy, cloneId, copies);
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

Entity* Scene::FindByName(std::string_view name) {
    for (const auto& e : m_entities) {
        if (e->name == name) return e.get();
    }
    return nullptr;
}

EntityId Scene::FindMainCamera() const {
    EntityId fallback = kNullEntity;
    for (const auto& e : m_entities) {
        const CameraComponent* cam = e->Get<CameraComponent>();
        if (!cam || !IsActiveInHierarchy(e->id)) continue;
        if (cam->main) return e->id;
        if (fallback == kNullEntity) fallback = e->id;
    }
    return fallback;
}

bool Scene::ApplyCamera(EntityId id, Camera& camera) const {
    const Entity* e = Find(id);
    const CameraComponent* cam = e ? e->Get<CameraComponent>() : nullptr;
    if (!cam) return false;

    const glm::mat4 world = WorldMatrix(id);
    const glm::vec3 forward = -glm::vec3(world[2]);
    if (glm::length(forward) < 1e-6f) return false;  // escala zero: sem direção
    const glm::vec3 dir = glm::normalize(forward);
    camera.position = glm::vec3(world[3]);
    camera.LookAt(camera.position + dir);

    glm::vec3 entityUp = glm::vec3(world[1]);
    entityUp -= glm::dot(entityUp, dir) * dir;
    if (glm::length(entityUp) > 1e-6f) {
        const glm::vec3 baseRight = camera.Right();
        const glm::vec3 baseUp = camera.Up();
        camera.roll = glm::degrees(std::atan2(-glm::dot(entityUp, baseRight), glm::dot(entityUp, baseUp)));
    }
    camera.fovY = cam->fov;
    camera.nearPlane = cam->nearPlane;
    camera.farPlane = cam->farPlane;
    return true;
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

std::vector<Light> Scene::CollectLights() const {
    std::vector<Light> lights;
    for (const auto& e : m_entities) {
        const LightComponent* light = e->Get<LightComponent>();
        if (!light || !IsActiveInHierarchy(e->id)) continue;
        lights.push_back(MakeLight(*light, WorldMatrix(e->id)));
    }
    return lights;
}

void Scene::Render(Renderer& renderer, const Camera& camera, const Framebuffer* target) const {
    renderer.BeginScene(camera, CollectLights(), environment, target);
    SubmitTo(renderer);
    renderer.EndScene();
}

void Scene::SubmitTo(Renderer& renderer) const {
    std::vector<glm::mat4> joints;
    for (const auto& e : m_entities) {
        const MeshRenderer* mr = e->Get<MeshRenderer>();
        if (!mr || !mr->mesh || !IsActiveInHierarchy(e->id)) continue;
        if (mr->mesh->IsSkinned()) {
            // Pose do Animator mais próximo (a própria entidade ou acima) que anima o esqueleto desta pele;
            // sem ele, a pose de repouso. As juntas ficam no espaço da entidade da malha.
            const Skin& skin = *mr->mesh->GetSkin();
            std::span<const glm::mat4> globals = skin.skeleton->restGlobal;
            for (const Entity* a = e.get(); a; a = Find(a->parent)) {
                const Animator* animator = a->Get<Animator>();
                if (animator && animator->PoseSkeleton() == skin.skeleton.get() && !animator->GlobalPose().empty()) {
                    globals = animator->GlobalPose();
                    break;
                }
            }
            skin.JointMatrices(globals, joints);
            renderer.SubmitSkinned(*mr->mesh, mr->Resolved(), WorldMatrix(e->id), joints, mr->shadows);
            continue;
        }
        renderer.Submit(*mr->mesh, mr->Resolved(), WorldMatrix(e->id), mr->shadows);
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

// Raio contra triângulo (Möller-Trumbore), dos dois lados. Retorna o t, se houver.
std::optional<float> RayTriangle(const glm::vec3& o, const glm::vec3& d, const glm::vec3& a, const glm::vec3& b,
                                 const glm::vec3& c) {
    const glm::vec3 e1 = b - a, e2 = c - a;
    const glm::vec3 p = glm::cross(d, e2);
    const float det = glm::dot(e1, p);
    if (std::abs(det) < 1e-12f) return std::nullopt;  // paralelo ao triângulo
    const float inv = 1.0f / det;
    const glm::vec3 s = o - a;
    const float u = glm::dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return std::nullopt;
    const glm::vec3 q = glm::cross(s, e1);
    const float v = glm::dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return std::nullopt;
    const float t = glm::dot(e2, q) * inv;
    return t >= 0.0f ? std::optional<float>(t) : std::nullopt;
}

// Primeiro triângulo da malha atingido (espaço local). Sem a cópia na CPU, a caixa dos limites.
std::optional<float> RayMesh(const glm::vec3& o, const glm::vec3& d, const Mesh& mesh) {
    const auto box = RayAabb(o, d, mesh.BoundsMin(), mesh.BoundsMax());
    if (!box) return std::nullopt;
    const auto& positions = mesh.Positions();
    const auto& indices = mesh.Indices();
    if (positions.empty() || indices.size() < 3) return box;
    std::optional<float> best;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        if (indices[i] >= positions.size() || indices[i + 1] >= positions.size() || indices[i + 2] >= positions.size()) {
            continue;
        }
        const auto t = RayTriangle(o, d, positions[indices[i]], positions[indices[i + 1]], positions[indices[i + 2]]);
        if (t && (!best || *t < *best)) best = t;
    }
    return best;
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

        if (auto t = RayMesh(o, d, *mr->mesh)) {
            if (!best || *t < best->distance) best = RaycastHit{e->id, *t};
        }
    }
    return best;
}

} // namespace motor
