#include "motor/physics/PhysicsSystem.h"

#include "PhysicsWorld.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/assets/AssetLoaders.h"
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
    for (const auto& entity : scene.Entities()) {
        if (entity->Has<ColliderComponent>() || entity->Has<CharacterControllerComponent>()) return true;
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
    w.WriteBack(scene);
    w.DispatchEvents(runtime);
    m_stats.writeMs = MillisecondsSince(start);

    m_stats.bodies = static_cast<int>(w.physics.GetNumBodies());
    m_stats.activeBodies = static_cast<int>(w.physics.GetNumActiveBodies(JPH::EBodyType::RigidBody));
    m_stats.characters = static_cast<int>(w.characters.size());
    m_stats.threads = w.threads;
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
    m_world->SyncBodies(*m_scene, 0.0f);
    m_world->SyncCharacters(*m_scene, 0.0f);
}

std::optional<PhysicsRaycastHit> PhysicsSystem::Raycast(const glm::vec3& origin, const glm::vec3& direction,
                                                        float maxDistance, EntityId ignore) const {
    if (!m_world || glm::length(direction) < 1e-8f || maxDistance <= 0.0f) return std::nullopt;
    const glm::vec3 dir = glm::normalize(direction);
    const JPH::RRayCast ray(ToJolt(origin), ToJolt(dir * maxDistance));
    JPH::RayCastResult hit;
    const RayBodyFilter filter(ignore);
    if (!m_world->physics.GetNarrowPhaseQueryNoLock().CastRay(ray, hit, {}, {}, filter)) return std::nullopt;

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

bool PhysicsSystem::HasBody(EntityId id) const {
    return m_world && (m_world->bodies.contains(id) || m_world->characters.contains(id));
}

MOTOR_RUNTIME_SYSTEM(Fisica, PhysicsSystem, kSystemOrderPhysics);

} // namespace motor
