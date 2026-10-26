// Mundo do Jolt: personagens, corpos -> Transforms e eventos de contato para os scripts.

#include "PhysicsWorld.h"

#include "motor/physics/PhysicsSystem.h"
#include "motor/assets/AssetLibrary.h"
#include "motor/assets/AssetLoaders.h"
#include "motor/core/Log.h"
#include "motor/renderer/Mesh.h"
#include "motor/scene/Components.h"
#include "motor/scene/SceneRuntime.h"
#include "motor/scene/Script.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <glm/common.hpp>
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
void PhysicsSystem::World::SyncCharacters(Scene& scene, float dt) {
    for (EntityId id : scene.EntitiesWith<CharacterControllerComponent>()) {
        Entity* entityPtr = scene.Find(id);
        const CharacterControllerComponent* controller =
            entityPtr ? entityPtr->Get<CharacterControllerComponent>() : nullptr;
        if (!controller || !scene.IsActiveInHierarchy(id)) continue;
        Entity& entity = *entityPtr;
        const glm::vec3 position = glm::vec3(scene.WorldMatrix(entity.id)[3]);

        auto it = characters.find(entity.id);
        if (it != characters.end()) {
            CharacterControllerComponent same = it->second.settings;
            same.useGravity = controller->useGravity;  // ligar/desligar a gravidade não refaz o personagem
            if (!(same == *controller)) {
                characters.erase(it);  // refaz com as medidas novas
                it = characters.end();
            } else {
                it->second.settings.useGravity = controller->useGravity;
            }
        }
        if (it == characters.end()) {
            CharacterRecord& r = characters[entity.id];
            r.settings = *controller;
            r.character = CreateCharacter(*controller, position, entity.id);
            r.written = position;
            it = characters.find(entity.id);
        }
        CharacterRecord& r = it->second;
        r.seen = frame;
        if (position != r.written) {  // teleporte pelo script ou pelo editor
            r.character->SetPosition(ToJolt(position));
            r.character->SetLinearVelocity(JPH::Vec3::sZero());
            r.walk = glm::vec3(0.0f);
            r.jump = 0.0f;
            r.velocity.reset();
            r.written = position;
        }
        if (dt > 0.0f) UpdateCharacter(r, dt);
    }
    for (auto it = characters.begin(); it != characters.end();) {
        if (it->second.seen != frame) {
            DropPairsOf(it->second.character->GetInnerBodyID());
            it = characters.erase(it);
        } else {
            ++it;
        }
    }
}

JPH::Ref<JPH::CharacterVirtual> PhysicsSystem::World::CreateCharacter(const CharacterControllerComponent& c,
                                                                      const glm::vec3& position, EntityId id) {
    const float radius = std::max(c.radius, 0.01f);
    const float halfCylinder = std::max(c.height * 0.5f - radius, 0.01f);
    JPH::Ref<JPH::CharacterVirtualSettings> s = new JPH::CharacterVirtualSettings();
    s->mShape = new JPH::CapsuleShape(halfCylinder, radius);  // centrada na origem da entidade
    s->mInnerBodyShape = s->mShape;
    s->mInnerBodyLayer = MakeLayer(true, c.layer.index);
    s->mMaxSlopeAngle = JPH::DegreesToRadians(std::clamp(c.maxSlope, 0.0f, 89.0f));
    s->mMass = std::max(c.mass, 0.1f);
    // O que toca abaixo do centro da esfera de baixo apoia o personagem; acima, só empurra.
    s->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), halfCylinder);
    JPH::Ref<JPH::CharacterVirtual> character =
        new JPH::CharacterVirtual(s, ToJolt(position), JPH::Quat::sIdentity(), id, &physics);
    // O corpo interno é o que os triggers e os raios veem: não pode dormir, senão os triggers param de ver.
    JPH::BodyLockWrite lock(physics.GetBodyLockInterfaceNoLock(), character->GetInnerBodyID());
    if (lock.Succeeded()) lock.GetBody().SetAllowSleeping(false);
    return character;
}

void PhysicsSystem::World::UpdateCharacter(CharacterRecord& r, float dt) {
    JPH::CharacterVirtual& ch = *r.character;
    const JPH::Vec3 gravity = r.settings.useGravity ? ToJolt(settings.gravity) : JPH::Vec3::sZero();
    ch.UpdateGroundVelocity();
    const JPH::Vec3 current = ch.GetLinearVelocity();
    const JPH::Vec3 ground = ch.GetGroundVelocity();
    JPH::Vec3 velocity;
    if (r.velocity) {
        // SetCharacterVelocity: a velocidade inteira deste passo; a vertical continua nos próximos, a horizontal
        // volta a ser só a de andar.
        velocity = ToJolt(*r.velocity);
        r.velocity.reset();
    } else if (ch.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround &&
               current.GetY() - ground.GetY() < 0.1f) {
        // No chão e sem estar subindo (logo depois do pulo ainda está "no chão"): anda com o chão.
        velocity = ground;
        if (r.jump > 0.0f) velocity += JPH::Vec3(0.0f, r.jump, 0.0f);
        velocity += JPH::Vec3(r.walk.x, 0.0f, r.walk.z);
    } else {
        velocity = JPH::Vec3(r.walk.x, current.GetY(), r.walk.z);
    }
    r.jump = 0.0f;
    velocity += gravity * dt;
    ch.SetLinearVelocity(velocity);

    JPH::CharacterVirtual::ExtendedUpdateSettings update;
    update.mWalkStairsStepUp = JPH::Vec3(0.0f, std::max(r.settings.stepHeight, 0.0f), 0.0f);
    if (r.settings.stepHeight <= 0.0f) update.mStickToFloorStepDown = JPH::Vec3::sZero();
    const JPH::ObjectLayer layer = MakeLayer(true, r.settings.layer.index);
    ch.ExtendedUpdate(dt, gravity, update, physics.GetDefaultBroadPhaseLayerFilter(layer),
                      physics.GetDefaultLayerFilter(layer), {}, {}, *temp);
}

void PhysicsSystem::World::WriteBack(Scene& scene, bool smooth) {
    smoothed.clear();
    auto remember = [&](EntityId id, const Transform& before, const Transform& after) {
        if (smooth && !SameTransform(before, after)) smoothed.push_back({id, before, after, after});
    };
    JPH::BodyInterface& bi = Bodies();
    // Só os acordados: com a maioria dormindo, não percorre todos os corpos perguntando um a um.
    activeBodies.clear();
    physics.GetActiveBodies(JPH::EBodyType::RigidBody, activeBodies);
    for (const JPH::BodyID bodyId : activeBodies) {
        const EntityId id = static_cast<EntityId>(bi.GetUserData(bodyId));
        auto found = bodies.find(id);
        if (found == bodies.end() || found->second.id != bodyId) continue;  // ex.: corpo interno de personagem
        BodyRecord& r = found->second;
        if (r.motion != JPH::EMotionType::Dynamic) continue;
        Entity* entity = scene.Find(id);
        if (!entity) continue;
        const Transform before = entity->transform;
        JPH::RVec3 position;
        JPH::Quat rotation;
        bi.GetPositionAndRotation(r.id, position, rotation);
        if (entity->parent == kNullEntity) {
            // Raiz: direto no Transform, sem decompor matriz (a escala fica exatamente como está).
            entity->transform.position = ToGlm(position);
            entity->transform.rotation = ToGlm(rotation);
        } else {
            const glm::mat4 world = glm::translate(glm::mat4(1.0f), ToGlm(position)) *
                                    glm::mat4_cast(ToGlm(rotation)) * glm::scale(glm::mat4(1.0f), r.scale);
            scene.SetWorldMatrix(id, world);
        }
        r.written = entity->transform;
        remember(id, before, entity->transform);
    }
    for (auto& [id, r] : characters) {
        Entity* entity = scene.Find(id);
        if (!entity) continue;
        const Transform before = entity->transform;
        const glm::vec3 position = ToGlm(r.character->GetPosition());
        if (entity->parent == kNullEntity) {
            entity->transform.position = position;
        } else {
            glm::mat4 world = scene.WorldMatrix(id);
            world[3] = glm::vec4(position, 1.0f);
            scene.SetWorldMatrix(id, world);
        }
        r.written = glm::vec3(scene.WorldMatrix(id)[3]);
        remember(id, before, entity->transform);
    }
}

void PhysicsSystem::World::ApplySmoothing(Scene& scene, float alpha) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    for (Smoothed& s : smoothed) {
        if (s.id == kNullEntity) continue;
        Entity* entity = scene.Find(s.id);
        if (!entity || !SameTransform(entity->transform, s.shown)) {
            s.id = kNullEntity;  // sumiu ou um script mexeu: fica como está, sem interpolar nem devolver
            continue;
        }
        Transform t = s.to;
        t.position = glm::mix(s.from.position, s.to.position, alpha);
        t.rotation = glm::slerp(s.from.rotation, s.to.rotation, alpha);
        t.scale = glm::mix(s.from.scale, s.to.scale, alpha);  // filho: a escala sai da matriz e varia um pouco
        entity->transform = t;
        s.shown = t;
    }
}

void PhysicsSystem::World::RestoreSmoothed(Scene& scene) {
    for (const Smoothed& s : smoothed) {
        if (s.id == kNullEntity) continue;
        Entity* entity = scene.Find(s.id);
        if (entity && SameTransform(entity->transform, s.shown)) entity->transform = s.to;
    }
    smoothed.clear();
}

bool PhysicsSystem::World::Awake(JPH::BodyID id) {
    JPH::BodyInterface& bi = Bodies();
    return bi.IsActive(id) || bi.GetMotionType(id) == JPH::EMotionType::Static;
}

void PhysicsSystem::World::WakeAround(JPH::BodyID id) {
    JPH::AABox bounds;
    {
        JPH::BodyLockRead lock(physics.GetBodyLockInterfaceNoLock(), id);
        if (!lock.Succeeded()) return;
        bounds = lock.GetBody().GetWorldSpaceBounds();
    }
    bounds.ExpandBy(JPH::Vec3::sReplicate(0.05f));
    // Acorda todos os vizinhos, de qualquer camada (os filtros padrão do Jolt aceitam tudo).
    Bodies().ActivateBodiesInAABox(bounds, JPH::BroadPhaseLayerFilter(), JPH::ObjectLayerFilter());
}

void PhysicsSystem::World::DropPairsOf(JPH::BodyID body) {
    for (auto it = pairs.begin(); it != pairs.end();) {
        const PairState& p = it->second;
        if (p.a == body || p.b == body) {
            pendingExits.push_back({p.entityA, p.entityB, p.trigger});
            it = pairs.erase(it);
        } else {
            ++it;
        }
    }
}

void PhysicsSystem::World::DispatchEvents(SceneRuntime& runtime) {
    JPH::BodyInterface& bi = Bodies();
    std::vector<ContactAdded> enters;
    for (const ContactAdded& e : contacts.m_added) {
        auto [it, inserted] = pairs.try_emplace(PairKey(e.a, e.b));
        PairState& p = it->second;
        p.asleep = false;
        if (inserted) {
            p.trigger = e.trigger;
            p.a = e.a;
            p.b = e.b;
            p.entityA = e.entityA;
            p.entityB = e.entityB;
            enters.push_back(e);
        }
    }
    std::vector<Exit> exits = std::move(pendingExits);
    pendingExits.clear();
    auto exitPair = [&](std::unordered_map<uint64_t, PairState>::iterator it) {
        exits.push_back({it->second.entityA, it->second.entityB, it->second.trigger});
        return pairs.erase(it);
    };
    for (const auto& [a, b] : contacts.m_removed) {
        auto it = pairs.find(PairKey(a, b));
        if (it == pairs.end()) continue;  // já saiu (corpo removido: o Exit saiu no DropPairsOf)
        if (physics.WereBodiesInContact(a, b)) continue;  // ainda há outra sub-forma encostando
        if (bi.IsAdded(a) && bi.IsAdded(b) && (!Awake(a) || !Awake(b))) {
            it->second.asleep = true;
            continue;
        }
        exitPair(it);
    }
    // Pares que dormiram: se os dois acordaram e o contato não voltou, eles se separaram.
    for (auto it = pairs.begin(); it != pairs.end();) {
        const PairState& p = it->second;
        if (p.asleep && Awake(p.a) && Awake(p.b) && !physics.WereBodiesInContact(p.a, p.b)) it = exitPair(it);
        else ++it;
    }
    contacts.m_added.clear();
    contacts.m_removed.clear();

    for (const ContactAdded& e : enters) {
        if (e.trigger) {
            runtime.CallEntityScripts(e.entityA, "OnTriggerEnter",
                                      [&](Script& s) { s.OnTriggerEnter(e.entityB); });
            runtime.CallEntityScripts(e.entityB, "OnTriggerEnter",
                                      [&](Script& s) { s.OnTriggerEnter(e.entityA); });
        } else {
            runtime.CallEntityScripts(e.entityA, "OnCollisionEnter", [&](Script& s) {
                s.OnCollisionEnter({e.entityB, e.point, -e.normal, e.speed});
            });
            runtime.CallEntityScripts(e.entityB, "OnCollisionEnter", [&](Script& s) {
                s.OnCollisionEnter({e.entityA, e.point, e.normal, e.speed});
            });
        }
    }
    for (const Exit& e : exits) {
        const std::string_view cb = e.trigger ? "OnTriggerExit" : "OnCollisionExit";
        runtime.CallEntityScripts(
            e.a, cb, [&](Script& s) { e.trigger ? s.OnTriggerExit(e.b) : s.OnCollisionExit(e.b); });
        runtime.CallEntityScripts(
            e.b, cb, [&](Script& s) { e.trigger ? s.OnTriggerExit(e.a) : s.OnCollisionExit(e.a); });
    }
}

const JPH::BodyID* PhysicsSystem::World::DynamicBody(EntityId id) const {
    auto it = bodies.find(id);
    if (it == bodies.end() || it->second.motion != JPH::EMotionType::Dynamic) return nullptr;
    return &it->second.id;
}
} // namespace motor
