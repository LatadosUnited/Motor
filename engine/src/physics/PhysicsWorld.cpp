// Mundo do Jolt: construção, formas dos colliders e corpos (componentes -> corpos).

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
PhysicsSystem::World::World(const PhysicsSettings& worldSettings) : settings((InitJolt(), worldSettings)) {
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    threads = settings.workerThreads < 0 ? static_cast<int>(hardware) - 1 : settings.workerThreads;
    threads = std::clamp(threads, 0, 64);
    temp = std::make_unique<JPH::TempAllocatorImplWithMallocFallback>(16 * 1024 * 1024);
    jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
    physics.Init(65536, 0, 65536, 65536, broadPhaseLayers, objectVsBroadPhase, objectPairs);
    physics.SetGravity(ToJolt(settings.gravity));
    physics.SetContactListener(&contacts);
}

PhysicsSystem::World::~World() {
    characters.clear();  // tiram o corpo interno do mundo antes de o mundo sumir
}

std::shared_ptr<MeshData> PhysicsSystem::World::MeshDataFor(const Mesh& mesh) {
    auto it = meshData.find(&mesh);
    if (it != meshData.end()) return it->second;
    // A fonte da configuração (se houver) tem a palavra final; senão, a cópia na CPU que toda Mesh guarda.
    auto data = std::make_shared<MeshData>();
    if (!settings.meshData || !settings.meshData(mesh, *data)) {
        data->vertices.clear();
        data->vertices.reserve(mesh.Positions().size());
        for (const glm::vec3& p : mesh.Positions()) data->vertices.push_back({p, glm::vec3(0.0f), glm::vec2(0.0f)});
        data->indices = mesh.Indices();
    }
    if (data->indices.size() < 3) data.reset();
    meshData.emplace(&mesh, data);
    return data;
}

JPH::ShapeRefC PhysicsSystem::World::BuildShape(const ColliderComponent& c, const glm::vec3& worldScale,
                                                const Mesh* mesh, JPH::EMotionType motion, EntityId id) {
    const glm::vec3 s = glm::max(glm::abs(worldScale), glm::vec3(1e-4f));
    glm::vec3 offset = c.center * s;
    JPH::ShapeRefC shape;

    auto box = [&](const glm::vec3& fullSize) {
        const glm::vec3 half = glm::max(fullSize * 0.5f, glm::vec3(0.005f));
        const float convexRadius = std::min(JPH::cDefaultConvexRadius, std::min(half.x, std::min(half.y, half.z)) * 0.5f);
        return JPH::ShapeRefC(new JPH::BoxShape(ToJolt(half), convexRadius));
    };

    switch (c.shape) {
        case ColliderShape::Box: shape = box(c.size * s); break;
        case ColliderShape::Sphere: shape = new JPH::SphereShape(std::max(c.radius * MaxComponent(s), 0.005f)); break;
        case ColliderShape::Capsule: {
            const float radius = std::max(c.radius * std::max(s.x, s.z), 0.005f);
            const float halfCylinder = c.height * s.y * 0.5f - radius;
            if (halfCylinder > 0.001f) shape = new JPH::CapsuleShape(halfCylinder, radius);
            else shape = new JPH::SphereShape(radius);
            break;
        }
        case ColliderShape::Mesh: {
            if (!mesh) {
                WarnOnce(id, "Física: Collider de malha sem MeshRenderer com malha; usando uma caixa 1x1x1");
                shape = box(s);
                break;
            }
            const std::shared_ptr<MeshData> data = MeshDataFor(*mesh);
            if (data) {
                shape = BuildMeshShape(*data, s, motion);
            } else {
                // Malha sem triângulos: a caixa dos limites.
                const glm::vec3 lo = mesh->BoundsMin(), hi = mesh->BoundsMax();
                shape = box((hi - lo) * s);
                offset += (lo + hi) * 0.5f * s;
            }
            break;
        }
    }
    if (!shape) shape = box(s);
    if (glm::length(offset) > 1e-6f) {
        shape = new JPH::RotatedTranslatedShape(ToJolt(offset), JPH::Quat::sIdentity(), shape);
    }
    return shape;
}

JPH::ShapeRefC PhysicsSystem::World::BuildMeshShape(const MeshData& data, const glm::vec3& s, JPH::EMotionType motion) {
    if (motion == JPH::EMotionType::Dynamic) {
        JPH::Array<JPH::Vec3> points;
        points.reserve(data.vertices.size());
        for (const Vertex& v : data.vertices) points.push_back(ToJolt(v.position * s));
        JPH::ConvexHullShapeSettings hull(points);
        const JPH::ShapeSettings::ShapeResult result = hull.Create();
        if (result.IsValid()) return result.Get();
        log::Warn("Física: casco convexo da malha falhou ({})", result.GetError().c_str());
        return nullptr;
    }
    JPH::VertexList vertices;
    vertices.reserve(data.vertices.size());
    for (const Vertex& v : data.vertices) {
        const glm::vec3 p = v.position * s;
        vertices.push_back(JPH::Float3(p.x, p.y, p.z));
    }
    JPH::IndexedTriangleList triangles;
    triangles.reserve(data.indices.size() / 3);
    for (size_t i = 0; i + 2 < data.indices.size(); i += 3) {
        triangles.push_back(JPH::IndexedTriangle(data.indices[i], data.indices[i + 1], data.indices[i + 2], 0));
    }
    JPH::MeshShapeSettings settingsMesh(std::move(vertices), std::move(triangles));
    const JPH::ShapeSettings::ShapeResult result = settingsMesh.Create();
    if (result.IsValid()) return result.Get();
    log::Warn("Física: malha do collider inválida ({})", result.GetError().c_str());
    return nullptr;
}

void PhysicsSystem::World::WarnOnce(EntityId id, const char* message) {
    if (warned.insert(id).second) log::Warn("{} (entidade {})", message, id);
}

void PhysicsSystem::World::SyncBodies(Scene& scene, float dt) {
    ++frame;
    JPH::BodyInterface& bi = Bodies();
    std::vector<JPH::BodyID> addStatic, addMoving, remove;

    for (const auto& entityPtr : scene.Entities()) {
        Entity& entity = *entityPtr;
        const ColliderComponent* collider = entity.Get<ColliderComponent>();
        const RigidbodyComponent* rigidbody = entity.Get<RigidbodyComponent>();
        if (!collider) {
            if (rigidbody) WarnOnce(entity.id, "Física: Rigidbody sem Collider é ignorado");
            continue;
        }
        if (!scene.IsActiveInHierarchy(entity.id)) continue;

        const MeshRenderer* renderer = collider->shape == ColliderShape::Mesh ? entity.Get<MeshRenderer>() : nullptr;
        const Mesh* mesh = renderer ? renderer->mesh.get() : nullptr;
        auto it = bodies.find(entity.id);
        if (it != bodies.end()) {
            BodyRecord& r = it->second;
            // Caminho comum (milhares de caixas): dinâmico que só a física mexeu, nada a fazer.
            if (r.motion == JPH::EMotionType::Dynamic && r.parent == entity.parent &&
                SameTransform(entity.transform, r.written) && rigidbody && r.rigidbody == *rigidbody &&
                r.collider == *collider && r.mesh == mesh) {
                r.seen = frame;
                continue;
            }
        }
        const Pose pose = Decompose(scene.WorldMatrix(entity.id));
        if (it != bodies.end()) {
            BodyRecord& r = it->second;
            const bool changed = !(r.collider == *collider) || r.hasRigidbody != (rigidbody != nullptr) ||
                                 (rigidbody && !(r.rigidbody == *rigidbody)) || r.mesh != mesh ||
                                 !SameScale(r.scale, pose.scale);
            if (changed) {
                // Refaz o corpo, mantendo a velocidade de quem já se movia.
                const JPH::Vec3 velocity = bi.GetLinearVelocity(r.id), angular = bi.GetAngularVelocity(r.id);
                WakeAround(r.id);
                DropPairsOf(r.id);
                bi.RemoveBody(r.id);
                bi.DestroyBody(r.id);
                bodies.erase(it);
                BodyRecord* created = CreateBody(entity, *collider, rigidbody, mesh, pose, addStatic, addMoving);
                if (created && created->motion == JPH::EMotionType::Dynamic) {
                    bi.SetLinearAndAngularVelocity(created->id, velocity, angular);
                }
                continue;
            }
            r.seen = frame;
            if (r.motion == JPH::EMotionType::Dynamic) {
                // Transform mexido por script ou pelo editor: teleporta (a velocidade continua).
                r.parent = entity.parent;
                if (!SameTransform(entity.transform, r.written)) {
                    WakeAround(r.id);
                    bi.SetPositionAndRotation(r.id, ToJolt(pose.position), ToJolt(pose.rotation), JPH::EActivation::Activate);
                    r.written = entity.transform;
                }
            } else if (r.motion == JPH::EMotionType::Kinematic) {
                const bool moved = pose.position != r.pose.position || pose.rotation != r.pose.rotation;
                // MoveKinematic dá a velocidade para chegar lá neste passo (e empurrar os outros); parado,
                // mais uma chamada zera a velocidade.
                if ((moved || r.kinematicMoving) && dt > 0.0f) {
                    bi.MoveKinematic(r.id, ToJolt(pose.position), ToJolt(pose.rotation), dt);
                } else if (moved) {
                    bi.SetPositionAndRotation(r.id, ToJolt(pose.position), ToJolt(pose.rotation), JPH::EActivation::Activate);
                }
                r.kinematicMoving = moved;
                r.pose = pose;
            } else if (pose.position != r.pose.position || pose.rotation != r.pose.rotation) {
                WakeAround(r.id);  // antes e depois: quem estava em cima e quem está no lugar novo
                bi.SetPositionAndRotation(r.id, ToJolt(pose.position), ToJolt(pose.rotation), JPH::EActivation::DontActivate);
                WakeAround(r.id);
                r.pose = pose;
            }
            continue;
        }
        CreateBody(entity, *collider, rigidbody, mesh, pose, addStatic, addMoving);
    }

    for (auto it = bodies.begin(); it != bodies.end();) {
        if (it->second.seen != frame) {
            WakeAround(it->second.id);
            DropPairsOf(it->second.id);
            remove.push_back(it->second.id);
            it = bodies.erase(it);
        } else {
            ++it;
        }
    }
    if (!remove.empty()) {
        bi.RemoveBodies(remove.data(), static_cast<int>(remove.size()));
        bi.DestroyBodies(remove.data(), static_cast<int>(remove.size()));
    }

    auto addBatch = [&](std::vector<JPH::BodyID>& ids, JPH::EActivation activation) {
        if (ids.empty()) return;
        JPH::BodyInterface::AddState state = bi.AddBodiesPrepare(ids.data(), static_cast<int>(ids.size()));
        bi.AddBodiesFinalize(ids.data(), static_cast<int>(ids.size()), state, activation);
    };
    addBatch(addStatic, JPH::EActivation::DontActivate);
    addBatch(addMoving, JPH::EActivation::Activate);
    // Muitos corpos de uma vez (início do Play): reorganiza a árvore da broad phase.
    if (addStatic.size() + addMoving.size() >= 64) physics.OptimizeBroadPhase();
}

PhysicsSystem::World::BodyRecord* PhysicsSystem::World::CreateBody(
    const Entity& entity, const ColliderComponent& collider, const RigidbodyComponent* rigidbody, const Mesh* mesh,
    const Pose& pose, std::vector<JPH::BodyID>& addStatic, std::vector<JPH::BodyID>& addMoving) {
    const JPH::EMotionType motion = !rigidbody ? JPH::EMotionType::Static
                                    : rigidbody->isKinematic ? JPH::EMotionType::Kinematic
                                                             : JPH::EMotionType::Dynamic;
    const JPH::ShapeRefC shape = BuildShape(collider, pose.scale, mesh, motion, entity.id);
    const JPH::ObjectLayer layer = MakeLayer(motion != JPH::EMotionType::Static, collider.layer.index);

    JPH::BodyCreationSettings settingsBody(shape, ToJolt(pose.position), ToJolt(pose.rotation), motion, layer);
    settingsBody.mUserData = entity.id;
    settingsBody.mFriction = std::max(collider.friction, 0.0f);
    settingsBody.mRestitution = std::clamp(collider.restitution, 0.0f, 1.0f);
    settingsBody.mIsSensor = collider.isTrigger;
    // Trigger também percebe personagens (corpo interno cinemático) e corpos cinemáticos.
    settingsBody.mCollideKinematicVsNonDynamic = collider.isTrigger;
    if (rigidbody) {
        settingsBody.mGravityFactor = rigidbody->useGravity ? 1.0f : 0.0f;
        settingsBody.mLinearDamping = std::max(rigidbody->linearDamping, 0.0f);
        settingsBody.mAngularDamping = std::max(rigidbody->angularDamping, 0.0f);
        if (motion == JPH::EMotionType::Dynamic) {
            settingsBody.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settingsBody.mMassPropertiesOverride.mMass = std::max(rigidbody->mass, 0.001f);
        } else {
            // Cinemático (pode ser malha, que não tem volume): massa não importa, mas precisa existir.
            settingsBody.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            settingsBody.mMassPropertiesOverride.SetMassAndInertiaOfSolidBox(JPH::Vec3::sReplicate(1.0f), 1.0f);
        }
    }

    JPH::Body* body = Bodies().CreateBody(settingsBody);
    if (!body) {
        WarnOnce(entity.id, "Física: limite de corpos atingido");
        return nullptr;
    }
    BodyRecord& r = bodies[entity.id];
    r.id = body->GetID();
    r.collider = collider;
    r.hasRigidbody = rigidbody != nullptr;
    if (rigidbody) r.rigidbody = *rigidbody;
    r.mesh = mesh;
    r.scale = pose.scale;
    r.motion = motion;
    r.written = entity.transform;
    r.parent = entity.parent;
    r.pose = pose;
    r.seen = frame;
    (motion == JPH::EMotionType::Static ? addStatic : addMoving).push_back(r.id);
    return &r;
}
} // namespace motor
