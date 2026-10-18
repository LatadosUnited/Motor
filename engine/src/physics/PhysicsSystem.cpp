#include "motor/physics/PhysicsSystem.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/assets/AssetLoaders.h"
#include "motor/core/Log.h"
#include "motor/renderer/Mesh.h"
#include "motor/scene/Components.h"
#include "motor/scene/SceneRuntime.h"
#include "motor/scene/Script.h"

// O Jolt exige o Jolt.h antes de qualquer outro header dele.
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

namespace {

// --- Camadas: estáticos não colidem entre si (chão com parede não gera trabalho). ---
constexpr JPH::ObjectLayer kLayerStatic = 0;
constexpr JPH::ObjectLayer kLayerMoving = 1;
constexpr JPH::uint kNumLayers = 2;

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return kNumLayers; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(layer));
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer.GetValue() == kLayerStatic ? "Estaticos" : "Moveis";
    }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override {
        return layer == kLayerMoving || broadPhase.GetValue() == kLayerMoving;
    }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == kLayerMoving || b == kLayerMoving;
    }
};

JPH::Vec3 ToJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
JPH::Quat ToJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
glm::vec3 ToGlm(JPH::Vec3Arg v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
glm::quat ToGlm(JPH::QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

void JoltTrace(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log::Trace("Jolt: {}", buffer);
}

// Alocador, fábrica e tipos do Jolt: uma vez por processo (nunca desfeitos: outros mundos podem vir depois).
void InitJolt() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = &JoltTrace;
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

uint64_t PairKey(JPH::BodyID a, JPH::BodyID b) {
    uint32_t x = a.GetIndexAndSequenceNumber(), y = b.GetIndexAndSequenceNumber();
    if (x > y) std::swap(x, y);
    return (uint64_t(x) << 32) | y;
}

// Posição, rotação e escala de uma matriz de mundo (T * R * S, sem cisalhamento).
struct Pose {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

Pose Decompose(const glm::mat4& m) {
    Pose pose;
    pose.position = glm::vec3(m[3]);
    glm::vec3 axes[3] = {glm::vec3(m[0]), glm::vec3(m[1]), glm::vec3(m[2])};
    glm::mat3 rotation(1.0f);
    for (int i = 0; i < 3; ++i) {
        const float length = glm::length(axes[i]);
        pose.scale[i] = length;
        if (length > 1e-8f) rotation[i] = axes[i] / length;
    }
    if (glm::determinant(rotation) < 0.0f) rotation[0] = -rotation[0];  // escala negativa: só o tamanho conta
    pose.rotation = glm::normalize(glm::quat_cast(rotation));
    return pose;
}

bool SameScale(const glm::vec3& a, const glm::vec3& b) {
    for (int i = 0; i < 3; ++i) {
        if (std::abs(a[i] - b[i]) > 1e-4f * std::max(1.0f, std::abs(b[i]))) return false;
    }
    return true;
}

bool SameTransform(const Transform& a, const Transform& b) {
    return a.position == b.position && a.rotation == b.rotation && a.scale == b.scale;
}

float MaxComponent(const glm::vec3& v) { return std::max(v.x, std::max(v.y, v.z)); }

// Eventos de contato coletados nas threads do Jolt e entregues aos scripts na thread do jogo.
struct ContactAdded {
    JPH::BodyID a, b;
    EntityId entityA = kNullEntity, entityB = kNullEntity;
    bool trigger = false;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};  // de A para B (para onde B é empurrado)
    float speed = 0.0f;
};

class ContactCollector final : public JPH::ContactListener {
public:
    void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold,
                        JPH::ContactSettings&) override {
        ContactAdded event;
        event.a = body1.GetID();
        event.b = body2.GetID();
        event.entityA = static_cast<EntityId>(body1.GetUserData());
        event.entityB = static_cast<EntityId>(body2.GetUserData());
        event.trigger = body1.IsSensor() || body2.IsSensor();
        event.normal = ToGlm(manifold.mWorldSpaceNormal);
        if (!manifold.mRelativeContactPointsOn1.empty()) {
            const JPH::RVec3 point = manifold.GetWorldSpaceContactPointOn1(0);
            event.point = ToGlm(point);
            const JPH::Vec3 relative = body1.GetPointVelocity(point) - body2.GetPointVelocity(point);
            event.speed = std::max(0.0f, relative.Dot(manifold.mWorldSpaceNormal));
        }
        std::lock_guard lock(m_mutex);
        m_added.push_back(event);
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
        std::lock_guard lock(m_mutex);
        m_removed.emplace_back(pair.GetBody1ID(), pair.GetBody2ID());
    }

    // Só fora do PhysicsSystem::Update (aí ninguém mais escreve).
    std::vector<ContactAdded> m_added;
    std::vector<std::pair<JPH::BodyID, JPH::BodyID>> m_removed;

private:
    std::mutex m_mutex;
};

// Raios: atravessam triggers e a entidade ignorada.
class RayBodyFilter final : public JPH::BodyFilter {
public:
    explicit RayBodyFilter(EntityId ignore) : m_ignore(ignore) {}
    bool ShouldCollideLocked(const JPH::Body& body) const override {
        return !body.IsSensor() && (m_ignore == kNullEntity || body.GetUserData() != m_ignore);
    }

private:
    EntityId m_ignore;
};

} // namespace

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

struct PhysicsSystem::World {
    struct BodyRecord {
        JPH::BodyID id;
        ColliderComponent collider;
        bool hasRigidbody = false;
        RigidbodyComponent rigidbody;
        const Mesh* mesh = nullptr;  // forma Malha
        glm::vec3 scale{1.0f};
        JPH::EMotionType motion = JPH::EMotionType::Static;
        Transform written;          // dinâmicos: Transform local que a física escreveu por último
        EntityId parent = kNullEntity;  // pai quando `written` foi escrito
        Pose pose;                  // estáticos e cinemáticos: pose do mundo aplicada por último
        bool kinematicMoving = false;
        uint32_t seen = 0;
    };

    struct CharacterRecord {
        JPH::Ref<JPH::CharacterVirtual> character;
        CharacterControllerComponent settings;
        glm::vec3 written{0.0f};  // posição no mundo escrita por último
        glm::vec3 walk{0.0f};     // velocidade pedida pelo script (só x e z)
        float jump = 0.0f;        // pulo pendente
        uint32_t seen = 0;
    };

    struct PairState {
        bool trigger = false;
        bool asleep = false;  // o Jolt tirou o contato porque um dos dois dormiu: continua "em contato"
        JPH::BodyID a, b;
        EntityId entityA = kNullEntity, entityB = kNullEntity;
    };

    struct Exit {
        EntityId a = kNullEntity, b = kNullEntity;
        bool trigger = false;
    };

    // InitJolt antes de qualquer membro: o JPH::PhysicsSystem já usa o alocador do Jolt ao ser construído.
    explicit World(const PhysicsSettings& worldSettings) : settings((InitJolt(), worldSettings)) {
        const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
        threads = settings.workerThreads < 0 ? static_cast<int>(hardware) - 1 : settings.workerThreads;
        threads = std::clamp(threads, 0, 64);
        temp = std::make_unique<JPH::TempAllocatorImplWithMallocFallback>(16 * 1024 * 1024);
        jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
        physics.Init(65536, 0, 65536, 65536, broadPhaseLayers, objectVsBroadPhase, objectPairs);
        physics.SetGravity(ToJolt(settings.gravity));
        physics.SetContactListener(&contacts);
    }

    ~World() {
        characters.clear();  // tiram o corpo interno do mundo antes de o mundo sumir
    }

    JPH::BodyInterface& Bodies() { return physics.GetBodyInterfaceNoLock(); }

    std::shared_ptr<MeshData> MeshDataFor(const Mesh& mesh) {
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

    // Forma do collider com a escala do mundo já aplicada. `motion`: malha só fica malha em corpo estático
    // ou cinemático (o Jolt não simula malha dinâmica): dinâmica vira o casco convexo.
    JPH::ShapeRefC BuildShape(const ColliderComponent& c, const glm::vec3& worldScale, const Mesh* mesh,
                              JPH::EMotionType motion, EntityId id) {
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

    JPH::ShapeRefC BuildMeshShape(const MeshData& data, const glm::vec3& s, JPH::EMotionType motion) {
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

    void WarnOnce(EntityId id, const char* message) {
        if (warned.insert(id).second) log::Warn("{} (entidade {})", message, id);
    }

    // Componentes -> corpos. Novos e alterados entram em lote (bem mais rápido que um a um com milhares).
    void SyncBodies(Scene& scene, float dt) {
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

    BodyRecord* CreateBody(const Entity& entity, const ColliderComponent& collider, const RigidbodyComponent* rigidbody,
                           const Mesh* mesh, const Pose& pose, std::vector<JPH::BodyID>& addStatic,
                           std::vector<JPH::BodyID>& addMoving) {
        const JPH::EMotionType motion = !rigidbody ? JPH::EMotionType::Static
                                        : rigidbody->isKinematic ? JPH::EMotionType::Kinematic
                                                                 : JPH::EMotionType::Dynamic;
        const JPH::ShapeRefC shape = BuildShape(collider, pose.scale, mesh, motion, entity.id);
        const JPH::ObjectLayer layer = motion == JPH::EMotionType::Static ? kLayerStatic : kLayerMoving;

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

    // Personagens: criados/removidos como os corpos; andam antes do passo do mundo.
    void SyncCharacters(Scene& scene, float dt) {
        for (const auto& entityPtr : scene.Entities()) {
            Entity& entity = *entityPtr;
            const CharacterControllerComponent* controller = entity.Get<CharacterControllerComponent>();
            if (!controller || !scene.IsActiveInHierarchy(entity.id)) continue;
            const glm::vec3 position = glm::vec3(scene.WorldMatrix(entity.id)[3]);

            auto it = characters.find(entity.id);
            if (it != characters.end() && !(it->second.settings == *controller)) {
                characters.erase(it);  // refaz com as medidas novas
                it = characters.end();
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

    JPH::Ref<JPH::CharacterVirtual> CreateCharacter(const CharacterControllerComponent& c, const glm::vec3& position,
                                                    EntityId id) {
        const float radius = std::max(c.radius, 0.01f);
        const float halfCylinder = std::max(c.height * 0.5f - radius, 0.01f);
        JPH::Ref<JPH::CharacterVirtualSettings> s = new JPH::CharacterVirtualSettings();
        s->mShape = new JPH::CapsuleShape(halfCylinder, radius);  // centrada na origem da entidade
        s->mInnerBodyShape = s->mShape;
        s->mInnerBodyLayer = kLayerMoving;
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

    void UpdateCharacter(CharacterRecord& r, float dt) {
        JPH::CharacterVirtual& ch = *r.character;
        const JPH::Vec3 gravity = ToJolt(settings.gravity);
        ch.UpdateGroundVelocity();
        const JPH::Vec3 current = ch.GetLinearVelocity();
        const JPH::Vec3 ground = ch.GetGroundVelocity();
        JPH::Vec3 velocity;
        // No chão e sem estar subindo (logo depois do pulo ainda está "no chão"): anda com o chão.
        if (ch.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround && current.GetY() - ground.GetY() < 0.1f) {
            velocity = ground;
            if (r.jump > 0.0f) velocity += JPH::Vec3(0.0f, r.jump, 0.0f);
        } else {
            velocity = JPH::Vec3(0.0f, current.GetY(), 0.0f);
        }
        r.jump = 0.0f;
        velocity += gravity * dt;
        velocity += JPH::Vec3(r.walk.x, 0.0f, r.walk.z);
        ch.SetLinearVelocity(velocity);

        JPH::CharacterVirtual::ExtendedUpdateSettings update;
        update.mWalkStairsStepUp = JPH::Vec3(0.0f, std::max(r.settings.stepHeight, 0.0f), 0.0f);
        if (r.settings.stepHeight <= 0.0f) update.mStickToFloorStepDown = JPH::Vec3::sZero();
        ch.ExtendedUpdate(dt, gravity, update, physics.GetDefaultBroadPhaseLayerFilter(kLayerMoving),
                          physics.GetDefaultLayerFilter(kLayerMoving), {}, {}, *temp);
    }

    // Corpos -> Transforms (só os dinâmicos acordados) e personagens -> Transforms.
    void WriteBack(Scene& scene) {
        JPH::BodyInterface& bi = Bodies();
        for (auto& [id, r] : bodies) {
            if (r.motion != JPH::EMotionType::Dynamic || !bi.IsActive(r.id)) continue;
            Entity* entity = scene.Find(id);
            if (!entity) continue;
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
        }
        for (auto& [id, r] : characters) {
            Entity* entity = scene.Find(id);
            if (!entity) continue;
            const glm::vec3 position = ToGlm(r.character->GetPosition());
            if (entity->parent == kNullEntity) {
                entity->transform.position = position;
            } else {
                glm::mat4 world = scene.WorldMatrix(id);
                world[3] = glm::vec4(position, 1.0f);
                scene.SetWorldMatrix(id, world);
            }
            r.written = glm::vec3(scene.WorldMatrix(id)[3]);
        }
    }

    // Acordado para fins de contato (estático nunca "acorda", mas também nunca dorme).
    bool Awake(JPH::BodyID id) {
        JPH::BodyInterface& bi = Bodies();
        return bi.IsActive(id) || bi.GetMotionType(id) == JPH::EMotionType::Static;
    }

    // Corpo que vai mudar de lugar ou sumir: acorda quem está encostado nele (o Jolt não faz isso sozinho, e
    // uma caixa dormindo em cima de outra que foi teleportada ficaria flutuando).
    void WakeAround(JPH::BodyID id) {
        JPH::AABox bounds;
        {
            JPH::BodyLockRead lock(physics.GetBodyLockInterfaceNoLock(), id);
            if (!lock.Succeeded()) return;
            bounds = lock.GetBody().GetWorldSpaceBounds();
        }
        bounds.ExpandBy(JPH::Vec3::sReplicate(0.05f));
        Bodies().ActivateBodiesInAABox(bounds, physics.GetDefaultBroadPhaseLayerFilter(kLayerMoving),
                                       physics.GetDefaultLayerFilter(kLayerMoving));
    }

    // Corpo saindo do mundo: quem estava em contato com ele recebe o Exit.
    void DropPairsOf(JPH::BodyID body) {
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

    // Contatos do passo -> OnCollision*/OnTrigger* dos scripts. Um par de entidades entra uma vez e sai quando
    // não sobra contato entre elas (WereBodiesInContact cobre as várias sub-formas, ex.: triângulos de malha).
    // O Jolt também tira os contatos de quem dorme e os devolve ao acordar: isso não vira Exit/Enter.
    void DispatchEvents(SceneRuntime& runtime) {
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

    const JPH::BodyID* DynamicBody(EntityId id) const {
        auto it = bodies.find(id);
        if (it == bodies.end() || it->second.motion != JPH::EMotionType::Dynamic) return nullptr;
        return &it->second.id;
    }

    PhysicsSettings settings;
    int threads = 0;
    std::unique_ptr<JPH::TempAllocator> temp;
    std::unique_ptr<JPH::JobSystemThreadPool> jobs;
    BroadPhaseLayers broadPhaseLayers;
    ObjectVsBroadPhase objectVsBroadPhase;
    ObjectPairs objectPairs;
    ContactCollector contacts;
    JPH::PhysicsSystem physics;  // depois do coletor de contatos e das camadas, que ele usa

    std::unordered_map<EntityId, BodyRecord> bodies;
    std::unordered_map<EntityId, CharacterRecord> characters;  // depois de physics: sai antes dele
    std::unordered_map<const Mesh*, std::shared_ptr<MeshData>> meshData;
    std::unordered_map<uint64_t, PairState> pairs;
    std::vector<Exit> pendingExits;
    std::unordered_set<EntityId> warned;
    uint32_t frame = 0;
    bool reportedUpdateError = false;
};

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
