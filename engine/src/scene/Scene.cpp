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
uint64_t g_structureVersion = 0;  // SceneStructureVersion(); a cena só é mexida na thread principal
uint64_t g_worldStamp = 0;        // EntityCache::stamp

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

uint64_t SceneStructureVersion() {
    return g_structureVersion;
}

void MarkSceneStructureChanged() {
    ++g_structureVersion;
}

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
    ++g_structureVersion;
    return components.emplace_back(type).Data();
}

void Entity::Remove(const ComponentType& type) {
    if (std::erase_if(components, [&](const ComponentPtr& c) { return &c.Type() == &type; })) ++g_structureVersion;
}

// --- Scene ------------------------------------------------------------------------

Scene::Scene(const Scene& other) : environment(other.environment), m_nextId(other.m_nextId) {
    ++g_structureVersion;
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
    ++g_structureVersion;
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
    DestroyEntities({id});
}

void Scene::DestroyEntities(const std::vector<EntityId>& roots) {
    std::vector<EntityId> doomed = Subtrees(roots);
    if (doomed.empty()) return;
    ++g_structureVersion;

    for (EntityId d : doomed) m_byId.erase(d);
    std::sort(doomed.begin(), doomed.end());
    std::erase_if(m_entities, [&](const auto& e) { return std::binary_search(doomed.begin(), doomed.end(), e->id); });
}

std::vector<EntityId> Scene::Subtrees(const std::vector<EntityId>& roots) const {
    // Vetores ordenados com busca binária em vez de conjuntos com hash: destruir 200 entidades por quadro
    // (spawn200) alocava um nó por entidade em cada conjunto.
    std::vector<EntityId> requested;
    for (EntityId id : roots) {
        if (Find(id)) requested.push_back(id);
    }
    if (requested.empty()) return {};
    std::sort(requested.begin(), requested.end());
    requested.erase(std::unique(requested.begin(), requested.end()), requested.end());
    auto isRequested = [&](EntityId id) { return std::binary_search(requested.begin(), requested.end(), id); };

    // Dono de cada entidade: a raiz pedida mais alta acima dela (ou ela mesma), kNullEntity se nenhuma. Sobe a
    // hierarquia sem guardar nada: na cena comum (quase tudo na raiz) é uma busca por entidade.
    auto ownerOf = [&](const Entity& e) {
        EntityId found = isRequested(e.id) ? e.id : kNullEntity;
        for (EntityId p = e.parent; p != kNullEntity;) {
            const Entity* up = Find(p);
            if (!up) break;
            if (isRequested(p)) found = p;
            p = up->parent;
        }
        return found;
    };

    std::vector<std::pair<EntityId, EntityId>> below;  // (raiz, descendente), na ordem da cena
    std::vector<EntityId> nested;                       // raízes pedidas dentro de outra
    for (const auto& e : m_entities) {
        if (e->parent == kNullEntity) continue;  // sem pai: só pode ser a própria raiz
        const EntityId o = ownerOf(*e);
        if (o == kNullEntity || o == e->id) continue;
        below.emplace_back(o, e->id);
        if (isRequested(e->id)) nested.push_back(e->id);
    }
    std::stable_sort(below.begin(), below.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::sort(nested.begin(), nested.end());

    std::vector<EntityId> result;
    std::vector<bool> emitted(requested.size(), false);
    for (EntityId root : roots) {
        const auto it = std::lower_bound(requested.begin(), requested.end(), root);
        if (it == requested.end() || *it != root) continue;  // inexistente
        const size_t index = static_cast<size_t>(it - requested.begin());
        if (emitted[index] || std::binary_search(nested.begin(), nested.end(), root)) continue;  // repetida ou dentro de outra
        emitted[index] = true;
        result.push_back(root);
        auto first = std::lower_bound(below.begin(), below.end(), root,
                                      [](const auto& pair, EntityId id) { return pair.first < id; });
        for (; first != below.end() && first->first == root; ++first) result.push_back(first->second);
    }
    return result;
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
    ++g_structureVersion;
    const EntityId cloneId = clone.id;
    copies.emplace_back(source.id, cloneId);

    for (EntityId child : children) {
        const Entity childCopy = *Find(child);
        CloneRecursive(childCopy, cloneId, copies);
    }
    return cloneId;
}

Entity* Scene::Find(EntityId id) {
    if (id == kNullEntity) return nullptr;  // o "pai" de quem está na raiz: sem busca no mapa
    auto it = m_byId.find(id);
    return it != m_byId.end() ? it->second : nullptr;
}

const Entity* Scene::Find(EntityId id) const {
    if (id == kNullEntity) return nullptr;
    auto it = m_byId.find(id);
    return it != m_byId.end() ? it->second : nullptr;
}

const std::vector<EntityId>& Scene::EntitiesWith(const ComponentType* type) const {
    static const std::vector<EntityId> kNone;
    if (!type) return kNone;  // tipo não registrado: ninguém tem
    if (m_byTypeVersion != g_structureVersion) {
        for (auto& [t, ids] : m_byType) ids.clear();  // mantém a memória das listas
        for (const auto& e : m_entities) {
            for (const ComponentPtr& c : e->components) m_byType[&c.Type()].push_back(e->id);
        }
        m_byTypeVersion = g_structureVersion;
    }
    auto it = m_byType.find(type);
    return it != m_byType.end() ? it->second : kNone;
}

Entity* Scene::FindByName(std::string_view name) {
    for (const auto& e : m_entities) {
        if (e->name == name) return e.get();
    }
    return nullptr;
}

EntityId Scene::FindMainCamera() const {
    EntityId fallback = kNullEntity;
    for (EntityId id : EntitiesWith<CameraComponent>()) {
        const Entity* e = Find(id);
        const CameraComponent* cam = e ? e->Get<CameraComponent>() : nullptr;
        if (!cam || !IsActiveInHierarchy(id)) continue;
        if (cam->main) return id;
        if (fallback == kNullEntity) fallback = id;
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
    const Entity* e = Find(id);
    return !e || IsActiveInHierarchy(*e);
}

bool Scene::IsActiveInHierarchy(const Entity& entity) const {
    for (const Entity* e = &entity; e; e = ParentOf(*e)) {
        if (!e->active) return false;
    }
    return true;
}

const Entity* Scene::ParentOf(const Entity& e) const {
    if (e.parent == kNullEntity) return nullptr;
    EntityCache& c = e.cache;
    // Destruir sobe a versão da estrutura; trocar de pai muda e.parent: nos dois casos, busca de novo. (A matriz
    // guardada se confere sozinha: pai diferente tem outro `stamp`.)
    if (c.structure != g_structureVersion || c.parent != e.parent || !c.parentPtr) {
        c.parentPtr = Find(e.parent);
        c.parent = e.parent;
        c.structure = g_structureVersion;
    }
    return c.parentPtr;
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
    const Entity* e = Find(id);
    return e ? CachedWorld(*e) : glm::mat4(1.0f);
}

const glm::mat4& Scene::CachedWorld(const Entity& e) const {
    // Confere de cima para baixo: o pai primeiro (que confere o dele), depois o transform desta. Numa hierarquia,
    // cada nível recalcula só quando algo acima ou nele mudou, em vez de remontar a cadeia inteira a cada consulta.
    const Entity* parent = ParentOf(e);
    const glm::mat4* parentWorld = parent ? &CachedWorld(*parent) : nullptr;
    const uint64_t parentStamp = parent ? parent->cache.stamp : 0;
    EntityCache& c = e.cache;
    const Transform& t = e.transform;
    if (c.valid && c.parentStamp == parentStamp && c.local.position == t.position && c.local.rotation == t.rotation &&
        c.local.scale == t.scale) {
        return c.world;
    }
    c.world = parentWorld ? *parentWorld * t.Matrix() : t.Matrix();
    c.local = t;
    c.parentStamp = parentStamp;
    c.stamp = ++g_worldStamp;
    c.valid = true;
    return c.world;
}

std::vector<Light> Scene::CollectLights() const {
    std::vector<Light> lights;
    for (EntityId id : EntitiesWith<LightComponent>()) {
        const Entity* e = Find(id);
        const LightComponent* light = e ? e->Get<LightComponent>() : nullptr;
        if (!light || !IsActiveInHierarchy(id)) continue;
        lights.push_back(MakeLight(*light, WorldMatrix(id)));
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
        if (!mr || !mr->mesh || !IsActiveInHierarchy(*e)) continue;
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
            renderer.SubmitSkinned(*mr->mesh, mr->Resolved(), CachedWorld(*e), joints, mr->shadows);
            continue;
        }
        renderer.Submit(*mr->mesh, mr->Resolved(), CachedWorld(*e), mr->shadows);
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
        if (!mr || !mr->mesh || !IsActiveInHierarchy(*e)) continue;

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
