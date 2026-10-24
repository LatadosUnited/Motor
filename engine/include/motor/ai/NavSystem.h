#pragma once

// Navegação do jogo (Fase 34). É um sistema do SceneRuntime (RuntimeSystem.h): no começo do Play gera o navmesh da
// cena (se ela tem uma NavMeshSurface) e, a cada FixedUpdate, antes da física e dos scripts, move os agentes
// (NavMeshAgent) pelo caminho até o destino.
//
// De onde vem o navmesh: os Colliders estáticos das entidades ativas (sem Rigidbody, sem trigger, sem agente e
// sem CharacterController). Caixa, esfera, cilindro e malha entram como são; a cápsula entra como cilindro.
// Mudou o cenário durante o jogo (porta aberta)? Rebuild() gera de novo; obstáculos que se mexem sem gerar tudo
// de novo são da Fase 36.
//
// Num script:
//   if (auto* nav = GetSystem<motor::NavSystem>()) {
//       nav->SetDestination(GetId(), alvo);            // o agente desta entidade vai até lá
//       if (nav->HasArrived(GetId())) { /* ... */ }
//   }

#include "motor/ai/NavMesh.h"
#include "motor/scene/RuntimeSystem.h"
#include "motor/scene/Scene.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace motor {

// Triângulos dos Colliders estáticos da cena, no mundo (o que o NavSystem usa para gerar o navmesh).
NavGeometry CollectNavGeometry(const Scene& scene);

// O que foi gerado no último Build/Rebuild (para o log, a barra de estatísticas e os testes).
struct NavStats {
    int triangles = 0;  // da geometria de entrada
    int polygons = 0;   // do navmesh
    double buildMs = 0.0;
    std::string error;  // vazio se deu certo (ou se a cena não tem NavMeshSurface)
};

class NavSystem final : public RuntimeSystem {
public:
    NavSystem();
    ~NavSystem() override;

    void OnStart(SceneRuntime& runtime) override;
    void OnFixedUpdate(SceneRuntime& runtime, float dt) override;
    void OnStop(SceneRuntime& runtime) override;

    // Gera o navmesh de novo a partir da cena como está agora. Os agentes refazem os caminhos. false se não há
    // NavMeshSurface ativa ou se a geração falhou (Stats().error).
    bool Rebuild();
    const NavMesh* GetNavMesh() const { return m_navMesh.get(); }
    const NavStats& Stats() const { return m_stats; }

    // --- Consultas (false/nullopt sem navmesh) ---
    bool FindPath(const glm::vec3& start, const glm::vec3& end, std::vector<glm::vec3>& corners) const;
    std::optional<glm::vec3> SamplePosition(const glm::vec3& position, float maxDistance = 2.0f) const;
    std::optional<NavRaycastHit> Raycast(const glm::vec3& start, const glm::vec3& end) const;

    // --- Agentes (entidade com NavMeshAgent) ---
    // Vai até `destination` (levado ao ponto mais perto no navmesh). false se não há caminho; destino em outra
    // ilha do navmesh vale como caminho até o ponto mais perto dele (IsPathPartial).
    bool SetDestination(EntityId id, const glm::vec3& destination);
    void Stop(EntityId id);               // esquece o destino e freia
    bool HasPath(EntityId id) const;      // tem destino e ainda não chegou
    bool HasArrived(EntityId id) const;   // recebeu um destino e chegou (até o próximo SetDestination)
    bool IsPathPartial(EntityId id) const;
    float RemainingDistance(EntityId id) const;  // pelo caminho; 0 sem caminho
    glm::vec3 GetVelocity(EntityId id) const;
    // Cantos do caminho ainda por andar, começando no próximo (vazio sem caminho).
    std::vector<glm::vec3> GetPath(EntityId id) const;
    // Teleporta o agente (levado ao navmesh) e esquece o caminho. false se o ponto está longe do navmesh.
    bool Warp(EntityId id, const glm::vec3& position);

private:
    struct Agent {
        std::vector<glm::vec3> corners;  // caminho atual (pés do agente no navmesh)
        size_t next = 0;                 // próximo canto
        glm::vec3 destination{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 targetSeen{0.0f};      // onde o alvo estava no último caminho
        bool hasDestination = false;
        bool arrived = false;
        bool partial = false;
        bool followingTarget = false;
        uint32_t seen = 0;               // passo em que o agente apareceu por último (some: o registro sai)
    };

    Agent* FindAgent(EntityId id);
    const Agent* FindAgent(EntityId id) const;
    Agent& AgentFor(EntityId id);
    bool PlanPath(Agent& agent, const glm::vec3& from, const glm::vec3& destination);
    void MoveAgent(EntityId id, Agent& agent, float dt);

    Scene* m_scene = nullptr;
    std::unique_ptr<NavMesh> m_navMesh;
    NavStats m_stats;
    std::unordered_map<EntityId, Agent> m_agents;
    uint32_t m_step = 0;
    bool m_warnedNoNavMesh = false;
};

} // namespace motor
