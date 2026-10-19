#pragma once

// Peças do Jolt usadas pelo PhysicsSystem e pelo World (só dentro da camada física): camadas de colisão,
// conversões glm <-> Jolt, inicialização, pose de uma matriz, coletor de contatos e filtro dos raios.

#include "motor/core/Log.h"
#include "motor/project/CollisionLayers.h"
#include "motor/scene/Scene.h"
#include "motor/scene/Transform.h"

// O Jolt exige o Jolt.h antes de qualquer outro header dele.
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <utility>
#include <vector>

namespace motor::physics_jolt {

// --- Camadas. ObjectLayer do Jolt = (móvel ? 16 : 0) + camada de colisão do projeto (CollisionLayers). Os
// estáticos não colidem entre si (chão com parede não gera trabalho); o resto segue a tabela do projeto. ---
constexpr JPH::uint kNumBroadPhaseLayers = 2;  // 0: estáticos, 1: móveis
constexpr JPH::ObjectLayer kMovingOffset = CollisionLayers::kCount;

inline JPH::ObjectLayer MakeLayer(bool moving, int collisionLayer) {
    return static_cast<JPH::ObjectLayer>((moving ? kMovingOffset : 0) + collisionLayer);
}
inline bool IsMoving(JPH::ObjectLayer layer) { return layer >= kMovingOffset; }
inline int CollisionLayerOf(JPH::ObjectLayer layer) { return layer % CollisionLayers::kCount; }

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return kNumBroadPhaseLayers; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(IsMoving(layer) ? 1 : 0);
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer.GetValue() == 0 ? "Estaticos" : "Moveis";
    }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override {
        return IsMoving(layer) || broadPhase.GetValue() == 1;
    }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    explicit ObjectPairs(const CollisionLayers& layers) : m_layers(layers) {}
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return (IsMoving(a) || IsMoving(b)) && m_layers.Collide(CollisionLayerOf(a), CollisionLayerOf(b));
    }

private:
    CollisionLayers m_layers;
};

// Raios e consultas: só as camadas da máscara (bit N = camada N).
class LayerMaskFilter final : public JPH::ObjectLayerFilter {
public:
    explicit LayerMaskFilter(uint32_t mask) : m_mask(mask) {}
    bool ShouldCollide(JPH::ObjectLayer layer) const override { return (m_mask >> CollisionLayerOf(layer)) & 1u; }

private:
    uint32_t m_mask;
};

inline JPH::Vec3 ToJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
inline JPH::Quat ToJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
inline glm::vec3 ToGlm(JPH::Vec3Arg v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
inline glm::quat ToGlm(JPH::QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

inline void JoltTrace(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log::Trace("Jolt: {}", buffer);
}

// Alocador, fábrica e tipos do Jolt: uma vez por processo (nunca desfeitos: outros mundos podem vir depois).
inline void InitJolt() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = &JoltTrace;
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

inline uint64_t PairKey(JPH::BodyID a, JPH::BodyID b) {
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

inline Pose Decompose(const glm::mat4& m) {
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

inline bool SameScale(const glm::vec3& a, const glm::vec3& b) {
    for (int i = 0; i < 3; ++i) {
        if (std::abs(a[i] - b[i]) > 1e-4f * std::max(1.0f, std::abs(b[i]))) return false;
    }
    return true;
}

inline bool SameTransform(const Transform& a, const Transform& b) {
    return a.position == b.position && a.rotation == b.rotation && a.scale == b.scale;
}

inline float MaxComponent(const glm::vec3& v) { return std::max(v.x, std::max(v.y, v.z)); }

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

// Consultas de área: triggers só se pedidos.
class AreaBodyFilter final : public JPH::BodyFilter {
public:
    explicit AreaBodyFilter(bool includeTriggers) : m_includeTriggers(includeTriggers) {}
    bool ShouldCollideLocked(const JPH::Body& body) const override { return m_includeTriggers || !body.IsSensor(); }

private:
    bool m_includeTriggers;
};

} // namespace motor::physics_jolt
