#include "motor/physics/PhysicsSystem.h"

#include "PhysicsWorld.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/assets/AssetLoaders.h"
#include "motor/core/FrameStats.h"
#include "motor/core/Log.h"
#include "motor/renderer/Mesh.h"
#include "motor/scene/Components.h"
#include "motor/scene/SceneRuntime.h"
#include "motor/scene/Script.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace motor {

using namespace physics_jolt;

// ---------------------------------------------------------------------------
// Configuração e fonte de malhas
// ---------------------------------------------------------------------------

PhysicsSettings& PhysicsSettings::Global() {
    static PhysicsSettings settings;
    return settings;
}

// ---------------------------------------------------------------------------
// Mundo do Jolt
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Sistema
// ---------------------------------------------------------------------------

namespace {

bool SceneHasPhysics(const Scene& scene) {
    if (!scene.EntitiesWith<ColliderComponent>().empty() || !scene.EntitiesWith<CharacterControllerComponent>().empty()) {
        return true;
    }
    // Terreno com colisão também é física (o personagem pode vir depois, criado por script).
    for (const EntityId id : scene.EntitiesWith<Terrain>()) {
        const Entity* e = scene.Find(id);
        const Terrain* terrain = e ? e->Get<Terrain>() : nullptr;
        if (terrain && terrain->collision) return true;
    }
    return false;
}

double MillisecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

PhysicsSystem::PhysicsSystem() = default;
PhysicsSystem::~PhysicsSystem() = default;

void PhysicsSystem::OnStart(SceneRuntime& runtime) {
    m_settings = PhysicsSettings::Global();
    Scene& scene = runtime.GetScene();
    m_scene = &scene;
    if (!SceneHasPhysics(scene)) return;  // sem física na cena: nem threads nem mundo
    m_world = std::make_unique<World>(m_settings);
    // Corpos prontos antes do OnStart dos scripts (raios e GetVelocity já funcionam lá).
    m_world->SyncBodies(scene, 0.0f);
    m_world->SyncCharacters(scene, 0.0f);
}

void PhysicsSystem::OnFixedUpdate(SceneRuntime& runtime, float dt) {
    Scene& scene = runtime.GetScene();
    m_scene = &scene;
    if (!m_world) {
        if (!SceneHasPhysics(scene)) return;
        m_world = std::make_unique<World>(m_settings);
    }
    World& w = *m_world;

    auto start = std::chrono::steady_clock::now();
    w.RestoreSmoothed(scene);  // a simulação parte do último passo, não da pose interpolada
    w.SyncBodies(scene, dt);
    w.SyncCharacters(scene, dt);
    m_stats.syncMs = MillisecondsSince(start);

    start = std::chrono::steady_clock::now();
    if (dt > 0.0f) {
        const float substep = m_settings.maxSubstep > 0.0f ? m_settings.maxSubstep : 1.0f / 60.0f;
        const int steps = std::clamp(static_cast<int>(std::ceil(dt / substep - 1e-4f)), 1, 8);
        const JPH::EPhysicsUpdateError error = w.physics.Update(dt, steps, w.temp.get(), w.jobs.get());
        if (error != JPH::EPhysicsUpdateError::None && !w.reportedUpdateError) {
            w.reportedUpdateError = true;
            log::Warn("Física: limites internos do Jolt atingidos (código {}); a simulação pode perder contatos",
                      static_cast<unsigned>(error));
        }
    }
    m_stats.stepMs = MillisecondsSince(start);

    start = std::chrono::steady_clock::now();
    w.WriteBack(scene, m_settings.interpolate);
    w.DispatchEvents(runtime);
    m_stats.writeMs = MillisecondsSince(start);

    m_stats.bodies = static_cast<int>(w.physics.GetNumBodies());
    m_stats.activeBodies = static_cast<int>(w.physics.GetNumActiveBodies(JPH::EBodyType::RigidBody));
    m_stats.characters = static_cast<int>(w.characters.size());
    m_stats.threads = w.threads;
    m_stats.terrainBlocksBuilt = w.terrainBlocksBuilt;

    // Partes do sistema "Fisica" nos contadores do quadro (--perf, MCP): somam nos vários passos fixos do quadro.
    FrameStats& frameStats = FrameStats::Instance();
    frameStats.AddCpu("Fisica sincronizar", m_stats.syncMs);
    frameStats.AddCpu("Fisica passo", m_stats.stepMs);
    frameStats.AddCpu("Fisica escrever", m_stats.writeMs);
    frameStats.SetValue("corpos ativos", m_stats.activeBodies);
}

void PhysicsSystem::OnUpdate(SceneRuntime& runtime, float) {
    if (m_world) m_world->ApplySmoothing(runtime.GetScene(), runtime.FixedAlpha());
}

void PhysicsSystem::OnStop(SceneRuntime&) {
    m_world.reset();
    m_scene = nullptr;
}

void PhysicsSystem::EnsureWorldAndBodies() {
    if (!m_scene) return;
    if (!m_world) {
        if (!SceneHasPhysics(*m_scene)) return;
        m_world = std::make_unique<World>(m_settings);
    }
    m_world->RestoreSmoothed(*m_scene);  // senão a pose interpolada viraria teleporte
    m_world->SyncBodies(*m_scene, 0.0f);
    m_world->SyncCharacters(*m_scene, 0.0f);
}

std::optional<PhysicsRaycastHit> PhysicsSystem::Raycast(const glm::vec3& origin, const glm::vec3& direction,
                                                        float maxDistance, EntityId ignore, uint32_t layerMask) const {
    if (!m_world || glm::length(direction) < 1e-8f || maxDistance <= 0.0f) return std::nullopt;
    const glm::vec3 dir = glm::normalize(direction);
    const JPH::RRayCast ray(ToJolt(origin), ToJolt(dir * maxDistance));
    JPH::RayCastResult hit;
    const RayBodyFilter filter(ignore);
    const LayerMaskFilter layers(layerMask);
    if (!m_world->physics.GetNarrowPhaseQueryNoLock().CastRay(ray, hit, {}, layers, filter)) return std::nullopt;

    PhysicsRaycastHit result;
    const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
    result.point = ToGlm(point);
    result.distance = hit.mFraction * maxDistance;
    JPH::BodyLockRead lock(m_world->physics.GetBodyLockInterfaceNoLock(), hit.mBodyID);
    if (lock.Succeeded()) {
        const JPH::Body& body = lock.GetBody();
        result.entity = static_cast<EntityId>(body.GetUserData());
        result.normal = ToGlm(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point));
    }
    return result;
}

namespace {
// Entidades distintas tocadas pela forma `shape` em `transform` (centro de massa no mundo).
std::vector<EntityId> Overlap(const JPH::PhysicsSystem& physics, const JPH::Shape& shape, const JPH::RMat44& transform,
                              uint32_t layerMask, bool includeTriggers) {
    JPH::CollideShapeSettings settings;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;  // dentro de um collider de malha também conta
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    const LayerMaskFilter layers(layerMask);
    const AreaBodyFilter bodies(includeTriggers);
    physics.GetNarrowPhaseQueryNoLock().CollideShape(&shape, JPH::Vec3::sReplicate(1.0f), transform, settings,
                                                     JPH::RVec3::sZero(), collector, {}, layers, bodies);
    std::vector<EntityId> found;
    const JPH::BodyLockInterfaceNoLock& locks = physics.GetBodyLockInterfaceNoLock();
    for (const JPH::CollideShapeResult& hit : collector.mHits) {
        JPH::BodyLockRead lock(locks, hit.mBodyID2);
        if (!lock.Succeeded()) continue;
        const auto id = static_cast<EntityId>(lock.GetBody().GetUserData());
        if (std::find(found.begin(), found.end(), id) == found.end()) found.push_back(id);
    }
    return found;
}
} // namespace

std::vector<EntityId> PhysicsSystem::OverlapSphere(const glm::vec3& center, float radius, uint32_t layerMask,
                                                   bool includeTriggers) const {
    if (!m_world || radius <= 0.0f) return {};
    JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();  // na pilha: a contagem de referências não pode apagá-lo
    return Overlap(m_world->physics, sphere, JPH::RMat44::sTranslation(ToJolt(center)), layerMask, includeTriggers);
}

std::vector<EntityId> PhysicsSystem::OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents,
                                                const glm::quat& rotation, uint32_t layerMask,
                                                bool includeTriggers) const {
    if (!m_world || halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f) return {};
    const glm::vec3 half = glm::max(halfExtents, glm::vec3(0.001f));
    const float convexRadius = std::min(JPH::cDefaultConvexRadius, std::min(half.x, std::min(half.y, half.z)) * 0.5f);
    JPH::BoxShape box(ToJolt(half), convexRadius);
    box.SetEmbedded();
    const JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(ToJolt(glm::normalize(rotation)), ToJolt(center));
    return Overlap(m_world->physics, box, transform, layerMask, includeTriggers);
}

std::optional<PhysicsRaycastHit> PhysicsSystem::SphereCast(const glm::vec3& origin, float radius,
                                                           const glm::vec3& direction, float maxDistance,
                                                           EntityId ignore, uint32_t layerMask) const {
    if (!m_world || radius <= 0.0f || glm::length(direction) < 1e-8f || maxDistance <= 0.0f) return std::nullopt;
    const glm::vec3 dir = glm::normalize(direction);
    JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();
    const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(
        &sphere, JPH::Vec3::sReplicate(1.0f), JPH::RMat44::sTranslation(ToJolt(origin)), ToJolt(dir * maxDistance));
    JPH::ShapeCastSettings settings;
    settings.mBackFaceModeTriangles = JPH::EBackFaceMode::IgnoreBackFaces;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    const LayerMaskFilter layers(layerMask);
    const RayBodyFilter bodies(ignore);
    m_world->physics.GetNarrowPhaseQueryNoLock().CastShape(cast, settings, JPH::RVec3::sZero(), collector, {}, layers,
                                                           bodies);
    if (!collector.HadHit()) return std::nullopt;
    const JPH::ShapeCastResult& hit = collector.mHit;
    PhysicsRaycastHit result;
    result.distance = hit.mFraction * maxDistance;
    result.point = ToGlm(hit.mContactPointOn2);
    const JPH::Vec3 axis = hit.mPenetrationAxis;
    if (axis.LengthSq() > 1e-12f) result.normal = ToGlm(-axis.Normalized());
    JPH::BodyLockRead lock(m_world->physics.GetBodyLockInterfaceNoLock(), hit.mBodyID2);
    if (lock.Succeeded()) result.entity = static_cast<EntityId>(lock.GetBody().GetUserData());
    return result;
}

bool PhysicsSystem::AddForce(EntityId id, const glm::vec3& force) {
    if (!m_world || !m_world->bodies.contains(id)) EnsureWorldAndBodies();
    const JPH::BodyID* body = m_world ? m_world->DynamicBody(id) : nullptr;
    if (!body) return false;
    m_world->Bodies().AddForce(*body, ToJolt(force));
    return true;
}

bool PhysicsSystem::AddImpulse(EntityId id, const glm::vec3& impulse) {
    if (!m_world || !m_world->bodies.contains(id)) EnsureWorldAndBodies();
    const JPH::BodyID* body = m_world ? m_world->DynamicBody(id) : nullptr;
    if (!body) return false;
    m_world->Bodies().AddImpulse(*body, ToJolt(impulse));
    return true;
}

bool PhysicsSystem::SetVelocity(EntityId id, const glm::vec3& velocity) {
    if (!m_world || !m_world->bodies.contains(id)) EnsureWorldAndBodies();
    const JPH::BodyID* body = m_world ? m_world->DynamicBody(id) : nullptr;
    if (!body) return false;
    m_world->Bodies().SetLinearVelocity(*body, ToJolt(velocity));
    return true;
}

glm::vec3 PhysicsSystem::GetVelocity(EntityId id) const {
    const JPH::BodyID* body = m_world ? m_world->DynamicBody(id) : nullptr;
    if (!body) return glm::vec3(0.0f);
    return ToGlm(m_world->physics.GetBodyInterfaceNoLock().GetLinearVelocity(*body));
}

void PhysicsSystem::MoveCharacter(EntityId id, const glm::vec3& velocity) {
    if (!m_world || !m_world->characters.contains(id)) EnsureWorldAndBodies();
    if (!m_world) return;
    auto it = m_world->characters.find(id);
    if (it != m_world->characters.end()) it->second.walk = glm::vec3(velocity.x, 0.0f, velocity.z);
}

bool PhysicsSystem::Jump(EntityId id, float speed) {
    if (!m_world || !m_world->characters.contains(id)) EnsureWorldAndBodies();
    if (!m_world || speed <= 0.0f || !IsGrounded(id)) return false;
    m_world->characters.at(id).jump = speed;
    return true;
}

bool PhysicsSystem::IsGrounded(EntityId id) const {
    if (!m_world) return false;
    auto it = m_world->characters.find(id);
    return it != m_world->characters.end() &&
           it->second.character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

glm::vec3 PhysicsSystem::GetCharacterVelocity(EntityId id) const {
    if (!m_world) return glm::vec3(0.0f);
    auto it = m_world->characters.find(id);
    return it != m_world->characters.end() ? ToGlm(it->second.character->GetLinearVelocity()) : glm::vec3(0.0f);
}

void PhysicsSystem::SetCharacterVelocity(EntityId id, const glm::vec3& velocity) {
    if (!m_world || !m_world->characters.contains(id)) EnsureWorldAndBodies();
    if (!m_world) return;
    auto it = m_world->characters.find(id);
    if (it != m_world->characters.end()) it->second.velocity = velocity;
}

bool PhysicsSystem::HasBody(EntityId id) const {
    return m_world && (m_world->bodies.contains(id) || m_world->characters.contains(id));
}

MOTOR_RUNTIME_SYSTEM(Fisica, PhysicsSystem, kSystemOrderPhysics);

} // namespace motor
