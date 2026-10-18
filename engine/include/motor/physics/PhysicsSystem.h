#pragma once

// Física do jogo (Fase 5), feita com o Jolt Physics. É um sistema do SceneRuntime (RuntimeSystem.h): roda no
// FixedUpdate antes dos scripts, cria os corpos a partir dos componentes e devolve as posições para os
// Transforms. O Jolt fica escondido no .cpp: scripts e editor só veem esta interface.
//
// Componentes (motor/scene/Components.h):
//   Collider              forma (caixa, esfera, cápsula, malha). Sozinho = corpo estático (chão, parede).
//   Collider + Rigidbody  corpo que cai e é empurrado (ou cinemático: segue o Transform).
//   CharacterController   personagem controlado por script (MoveCharacter, Jump), sem Collider.
//
// Em cada FixedUpdate: corpos novos/removidos/alterados entram no mundo -> personagens andam -> o mundo
// avança -> Transforms recebem as posições -> OnCollision/OnTrigger dos scripts -> OnFixedUpdate dos scripts.
// Mudar o Transform de um corpo pelo script ou pelo editor teleporta o corpo; mudar um componente refaz o corpo.
//
// Num script:
//   if (auto* fisica = GetSystem<motor::PhysicsSystem>()) {
//       fisica->MoveCharacter(GetId(), direcao * velocidade);
//       if (WasKeyPressed(motor::Key::Space)) fisica->Jump(GetId(), 5.0f);
//   }

#include "motor/scene/RuntimeSystem.h"
#include "motor/scene/Scene.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace motor {

class Mesh;
struct MeshData;
class AssetLibrary;

// Fonte opcional dos triângulos de um Collider de forma Malha. true se preencheu. Sem fonte (ou se ela devolver
// false), vale a cópia na CPU que toda Mesh guarda (Mesh::Positions/Indices), inclusive Cubo e Plano.
using MeshDataSource = std::function<bool(const Mesh& mesh, MeshData& out)>;

// Configuração global da física, lida a cada Play (cada PhysicsSystem copia ao iniciar).
struct PhysicsSettings {
    // Threads do job system do Jolt, além da thread do jogo (que também ajuda). -1 = núcleos da CPU - 1.
    int workerThreads = -1;
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    // Subpassos de colisão por FixedUpdate: um a cada `maxSubstep` segundos (passos grandes atravessam paredes).
    float maxSubstep = 1.0f / 60.0f;
    // Troca os triângulos do Collider de malha (testes); normalmente vazio (usa a cópia na CPU da Mesh).
    MeshDataSource meshData;

    static PhysicsSettings& Global();
};

struct PhysicsRaycastHit {
    EntityId entity = kNullEntity;
    glm::vec3 point{0.0f};   // no mundo
    glm::vec3 normal{0.0f};  // da superfície atingida
    float distance = 0.0f;
};

// Tempos do último passo, em milissegundos (para o benchmark e para a barra de estatísticas).
struct PhysicsStats {
    int bodies = 0;        // corpos no mundo (inclui o corpo interno dos personagens)
    int activeBodies = 0;  // acordados (corpos parados "dormem" e quase não custam)
    int characters = 0;
    int threads = 0;       // threads do job system
    double syncMs = 0.0;   // componentes -> corpos e personagens
    double stepMs = 0.0;   // passo do Jolt
    double writeMs = 0.0;  // corpos -> Transforms e eventos
};

class PhysicsSystem final : public RuntimeSystem {
public:
    PhysicsSystem();
    ~PhysicsSystem() override;

    void OnStart(SceneRuntime& runtime) override;
    void OnFixedUpdate(SceneRuntime& runtime, float dt) override;
    void OnStop(SceneRuntime& runtime) override;

    // Primeiro corpo no raio (direction não precisa ser normalizada), até maxDistance. `ignore`: entidade
    // que o raio atravessa (ex.: o próprio jogador). Triggers não contam.
    std::optional<PhysicsRaycastHit> Raycast(const glm::vec3& origin, const glm::vec3& direction,
                                             float maxDistance = 1000.0f, EntityId ignore = kNullEntity) const;

    // --- Rigidbody (dinâmico). false/zero se a entidade ainda não tem corpo (criado no próximo passo). ---
    bool AddForce(EntityId id, const glm::vec3& force);      // newtons, durante o próximo passo
    bool AddImpulse(EntityId id, const glm::vec3& impulse);  // muda a velocidade na hora (kg·m/s)
    bool SetVelocity(EntityId id, const glm::vec3& velocity);
    glm::vec3 GetVelocity(EntityId id) const;

    // --- Controlador de personagem ---
    // Velocidade de andar (m/s; a parte vertical é ignorada). Vale até a próxima chamada: zero para parar.
    void MoveCharacter(EntityId id, const glm::vec3& velocity);
    // Pula com essa velocidade para cima se estiver no chão (aplicado no próximo passo). true se vai pular.
    bool Jump(EntityId id, float speed);
    bool IsGrounded(EntityId id) const;
    glm::vec3 GetCharacterVelocity(EntityId id) const;

    bool HasBody(EntityId id) const;  // corpo ou personagem já criado para a entidade
    const PhysicsStats& Stats() const { return m_stats; }

private:
    struct World;
    void EnsureWorldAndBodies();
    std::unique_ptr<World> m_world;  // criado no primeiro passo com algo de física na cena
    Scene* m_scene = nullptr;
    PhysicsSettings m_settings;
    PhysicsStats m_stats;
};

} // namespace motor
