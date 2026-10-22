#pragma once

// Mundo do Jolt de um PhysicsSystem (só dentro da camada física): corpos e personagens criados a partir dos
// componentes, o passo e os eventos de contato. Métodos em PhysicsWorld.cpp (formas e corpos) e
// PhysicsWorldEvents.cpp (personagens, volta para os Transforms e eventos).

#include "motor/assets/AssetLoaders.h"
#include "motor/physics/PhysicsSystem.h"
#include "motor/scene/Components.h"

#include "PhysicsJolt.h"

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace motor {

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
        physics_jolt::Pose pose;                  // estáticos e cinemáticos: pose do mundo aplicada por último
        bool kinematicMoving = false;
        uint32_t seen = 0;
    };

    struct CharacterRecord {
        JPH::Ref<JPH::CharacterVirtual> character;
        CharacterControllerComponent settings;
        glm::vec3 written{0.0f};  // posição no mundo escrita por último
        glm::vec3 walk{0.0f};     // velocidade pedida pelo script (só x e z)
        float jump = 0.0f;        // pulo pendente
        std::optional<glm::vec3> velocity;  // SetCharacterVelocity pendente (substitui andar e pular no passo)
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

    // Corpo ou personagem que o último passo mexeu: Transform local antes e depois do passo, e o que a interpolação
    // pôs no Transform por último (para saber se um script mexeu nele depois).
    struct Smoothed {
        EntityId id = kNullEntity;
        Transform from;
        Transform to;
        Transform shown;
    };

    // InitJolt antes de qualquer membro: o JPH::PhysicsSystem já usa o alocador do Jolt ao ser construído.
    explicit World(const PhysicsSettings& worldSettings);

    ~World();

    JPH::BodyInterface& Bodies() { return physics.GetBodyInterfaceNoLock(); }

    std::shared_ptr<MeshData> MeshDataFor(const Mesh& mesh);

    // Forma do collider com a escala do mundo já aplicada. `motion`: malha só fica malha em corpo estático
    // ou cinemático (o Jolt não simula malha dinâmica): dinâmica vira o casco convexo.
    JPH::ShapeRefC BuildShape(const ColliderComponent& c, const glm::vec3& worldScale, const Mesh* mesh,
                              JPH::EMotionType motion, EntityId id);

    JPH::ShapeRefC BuildMeshShape(const MeshData& data, const glm::vec3& s, JPH::EMotionType motion);

    void WarnOnce(EntityId id, const char* message);

    // Componentes -> corpos. Novos e alterados entram em lote (bem mais rápido que um a um com milhares).
    void SyncBodies(Scene& scene, float dt);

    BodyRecord* CreateBody(const Entity& entity, const ColliderComponent& collider, const RigidbodyComponent* rigidbody,
                           const Mesh* mesh, const physics_jolt::Pose& pose, std::vector<JPH::BodyID>& addStatic,
                           std::vector<JPH::BodyID>& addMoving);

    // Personagens: criados/removidos como os corpos; andam antes do passo do mundo.
    void SyncCharacters(Scene& scene, float dt);

    JPH::Ref<JPH::CharacterVirtual> CreateCharacter(const CharacterControllerComponent& c, const glm::vec3& position,
                                                    EntityId id);

    void UpdateCharacter(CharacterRecord& r, float dt);

    // Corpos -> Transforms (só os dinâmicos acordados) e personagens -> Transforms. Com `smooth`, guarda em
    // `smoothed` quem mudou de lugar, com o Transform de antes e o de depois.
    void WriteBack(Scene& scene, bool smooth);

    // Interpolação (só visual): ApplySmoothing põe nos Transforms a mistura entre o penúltimo e o último passo
    // (alpha 0 a 1); RestoreSmoothed, antes de cada passo, devolve o Transform do último passo, para a simulação
    // nunca ver a pose interpolada. Transform mexido por script depois da interpolação fica como o script deixou
    // (vira teleporte no SyncBodies, como sempre).
    void ApplySmoothing(Scene& scene, float alpha);
    void RestoreSmoothed(Scene& scene);

    // Acordado para fins de contato (estático nunca "acorda", mas também nunca dorme).
    bool Awake(JPH::BodyID id);

    // Corpo que vai mudar de lugar ou sumir: acorda quem está encostado nele (o Jolt não faz isso sozinho, e
    // uma caixa dormindo em cima de outra que foi teleportada ficaria flutuando).
    void WakeAround(JPH::BodyID id);

    // Corpo saindo do mundo: quem estava em contato com ele recebe o Exit.
    void DropPairsOf(JPH::BodyID body);

    // Contatos do passo -> OnCollision*/OnTrigger* dos scripts. Um par de entidades entra uma vez e sai quando
    // não sobra contato entre elas (WereBodiesInContact cobre as várias sub-formas, ex.: triângulos de malha).
    // O Jolt também tira os contatos de quem dorme e os devolve ao acordar: isso não vira Exit/Enter.
    void DispatchEvents(SceneRuntime& runtime);

    const JPH::BodyID* DynamicBody(EntityId id) const;

    PhysicsSettings settings;
    int threads = 0;
    std::unique_ptr<JPH::TempAllocator> temp;
    std::unique_ptr<JPH::JobSystemThreadPool> jobs;
    physics_jolt::BroadPhaseLayers broadPhaseLayers;
    physics_jolt::ObjectVsBroadPhase objectVsBroadPhase;
    physics_jolt::ObjectPairs objectPairs{settings.layers};  // tabela de camadas do projeto
    physics_jolt::ContactCollector contacts;
    JPH::PhysicsSystem physics;  // depois do coletor de contatos e das camadas, que ele usa

    std::unordered_map<EntityId, BodyRecord> bodies;
    std::unordered_map<EntityId, CharacterRecord> characters;  // depois de physics: sai antes dele
    std::unordered_map<const Mesh*, std::shared_ptr<MeshData>> meshData;
    std::unordered_map<uint64_t, PairState> pairs;
    std::vector<Exit> pendingExits;
    std::vector<Smoothed> smoothed;  // do último passo
    std::unordered_set<EntityId> warned;
    uint32_t frame = 0;
    bool reportedUpdateError = false;
};

} // namespace motor
