#pragma once

// Navegação do jogo (Fase 34). É um sistema do SceneRuntime (RuntimeSystem.h): no começo do Play carrega o navmesh
// da cena (o .mnav da NavMeshSurface, gerado no editor) ou, sem arquivo, gera um na hora; a cada FixedUpdate, antes da
// física e dos scripts, move os agentes (NavMeshAgent) pelo caminho até o destino.
//
// Os agentes andam juntos, como uma multidão (DetourCrowd, Fase 35): desviam uns dos outros e não se atravessam.
// SetDestination busca o caminho na hora; quem segue um alvo e os caminhos que a multidão refaz entram numa fila
// dividida entre os passos, para muitos agentes não pesarem num passo só.
//
// Agente com Controlador de personagem (CharacterController): quem anda é a física. O NavSystem pede a velocidade
// ao PhysicsSystem (MoveCharacter) e a Jolt empurra caixas, para em paredes e cuida da gravidade; a multidão volta
// para onde o corpo chegou a cada passo, presa ao navmesh, e a velocidade pedida leva o corpo de volta a ele. Os pés
// ficam na base da cápsula (a altura da base do agente não conta).
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
#include "motor/scene/NavMeshSurface.h"
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

class NavCrowd;
class PhysicsSystem;
struct NavMeshAgent;

// Triângulos dos Colliders estáticos da cena, no mundo (o que o NavSystem usa para gerar o navmesh).
NavGeometry CollectNavGeometry(const Scene& scene);

// O que foi gerado ou carregado por último (para o log, a barra de estatísticas e os testes).
struct NavStats {
    int triangles = 0;  // da geometria de entrada (com o arquivo: da cena como está agora)
    int polygons = 0;   // do navmesh
    double buildMs = 0.0;
    bool fromFile = false;  // veio do .mnav da superfície
    bool stale = false;     // veio do .mnav, mas o cenário ou os parâmetros mudaram depois de gerá-lo
    std::string error;  // vazio se deu certo (ou se a cena não tem NavMeshSurface)
};

NavMeshBuildSettings NavSettingsFrom(const NavMeshSurface& surface);
// Gera o navmesh da cena com os parâmetros da superfície: o que o NavSystem faz sem arquivo e o que o botão "Gerar
// navmesh" do Inspector grava no .mnav. nullptr se falhar (`stats->error` diz por quê).
std::unique_ptr<NavMesh> BuildSceneNavMesh(const Scene& scene, const NavMeshSurface& surface, NavStats* stats = nullptr);

class NavSystem final : public RuntimeSystem {
public:
    NavSystem();
    ~NavSystem() override;

    void OnStart(SceneRuntime& runtime) override;
    void OnFixedUpdate(SceneRuntime& runtime, float dt) override;
    void OnStop(SceneRuntime& runtime) override;

    // Gera o navmesh de novo a partir da cena como está agora (sem ler nem gravar o .mnav). Os agentes refazem os
    // caminhos. false se não há NavMeshSurface ativa ou se a geração falhou (Stats().error).
    bool Rebuild();
    const NavMesh* GetNavMesh() const { return m_navMesh.get(); }
    const NavStats& Stats() const { return m_stats; }
    // Sobe a cada navmesh novo (gerado, lido ou descartado): quem desenha o navmesh sabe quando refazer.
    uint64_t Version() const { return m_version; }

    // --- Consultas (false/nullopt sem navmesh) ---
    // Só pelas áreas de `areaMask` (um bit por área; motor/scene/NavMeshArea.h), pesando o custo de cada uma.
    bool FindPath(const glm::vec3& start, const glm::vec3& end, std::vector<glm::vec3>& corners,
                  uint16_t areaMask = kNavAllAreas) const;
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
    bool IsOnLink(EntityId id) const;     // atravessando uma ligação (NavMeshLink: pulo, escada)
    float RemainingDistance(EntityId id) const;  // pelo caminho; 0 sem caminho
    glm::vec3 GetVelocity(EntityId id) const;
    // Cantos do caminho ainda por andar, começando no próximo (vazio sem caminho).
    std::vector<glm::vec3> GetPath(EntityId id) const;
    // Teleporta o agente (levado ao navmesh) e esquece o caminho. false se o ponto está longe do navmesh.
    bool Warp(EntityId id, const glm::vec3& position);

private:
    // Travessia de uma ligação (NavMeshLink): parado na multidão, o agente vai até a ponta de saída, atravessa (em
    // arco ou reto) e volta a andar com a multidão na ponta de chegada.
    struct Crossing {
        bool active = false;
        glm::vec3 from{0.0f};   // pés quando entrou
        glm::vec3 start{0.0f};
        glm::vec3 end{0.0f};
        float arc = 0.0f;       // altura do arco (0 = reto)
        float approach = 0.0f;  // s até a ponta de saída
        float duration = 0.0f;  // s da travessia inteira
        float time = 0.0f;
        glm::vec3 velocity{0.0f};
    };

    struct Agent {
        int crowd = -1;                  // índice na multidão (-1: ainda fora dela)
        glm::vec3 destination{0.0f};
        glm::vec3 written{0.0f};         // pés onde a entidade estava ou foi posta por último (mudou: teleporte)
        glm::vec3 targetSeen{0.0f};      // onde o alvo estava no último caminho
        float closest = 0.0f;            // menor distância do destino desde o último caminho
        float stalled = 0.0f;            // s sem chegar mais perto (encostado em outros: chegou)
        bool hasDestination = false;
        bool arrived = false;
        bool followingTarget = false;
        Crossing crossing;
        uint32_t seen = 0;               // passo em que o agente apareceu por último (some: o registro sai)
    };

    Agent* FindAgent(EntityId id);
    const Agent* FindAgent(EntityId id) const;
    Agent& AgentFor(EntityId id);
    float LiftOf(EntityId id, const NavMeshAgent& settings) const;  // da origem da entidade aos pés
    glm::vec3 FeetOf(EntityId id, const NavMeshAgent& settings) const;
    bool IsPhysical(EntityId id) const;  // tem CharacterController: anda pela física
    bool LoadFromFile(const NavMeshSurface& surface);
    void ReplanAgents();
    void ResetCrowd();
    void ApplyAreaCosts();
    void GrowCrowd();
    bool AddToCrowd(EntityId id, Agent& agent, const glm::vec3& feet);
    void PrepareAgent(EntityId id, Agent& agent, const NavMeshAgent& settings);
    static void NewDestination(Agent& agent, const glm::vec3& destination);
    bool PlanPath(Agent& agent, const glm::vec3& destination);
    void FollowTarget(Agent& agent, const NavMeshAgent& settings);
    void ApplyAgent(EntityId id, Agent& agent, const NavMeshAgent& settings, float dt);
    void Turn(EntityId id, const NavMeshAgent& settings, const glm::vec3& velocity, float dt, bool moved);
    float ArcOf(const glm::vec3& start, const glm::vec3& end) const;
    void StartCrossing(EntityId id, Agent& agent, const NavMeshAgent& settings, const glm::vec3& start,
                       const glm::vec3& end);
    void AdvanceCrossing(EntityId id, Agent& agent, const NavMeshAgent& settings, float dt);

    Scene* m_scene = nullptr;
    PhysicsSystem* m_physics = nullptr;  // corpo dos agentes com CharacterController
    std::unique_ptr<NavMesh> m_navMesh;
    std::unique_ptr<NavCrowd> m_crowd;  // depois do navmesh: sai antes dele
    NavStats m_stats;
    std::unordered_map<EntityId, Agent> m_agents;
    int m_crowdCapacity = 32;
    uint32_t m_step = 0;
    uint64_t m_version = 0;
    bool m_warnedNoNavMesh = false;
};

} // namespace motor
