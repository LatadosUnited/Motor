#pragma once

// Percepção do jogo (Fase 37). É um sistema do SceneRuntime (RuntimeSystem.h): a cada FixedUpdate, depois da física
// e antes dos scripts, cada entidade com Perception olha os Alvos de percepção (PerceptionTarget) dos grupos que
// percebe. Ela vê um alvo quando ele está dentro do alcance (vezes a visibilidade do alvo) e do cone de visão, ou
// perto o bastante para perceber em qualquer direção, e nada tapa a linha de visada. A linha vai dos olhos até o
// ponto olhado do alvo pelo PhysicsSystem::Raycast: um Collider no meio, que não seja do alvo nem de quem olha,
// tapa. Sem física na cena, nada tapa.
//
// Audição: um ruído (MakeNoise, de um script: passos, um tiro, uma pedra que cai) chega a quem está a até `range`
// metros dele, vezes a audição de cada um (Perception::hearing), medidos dos olhos. Os sons que tocam na Fonte de
// áudio (AudioSource) de um Alvo de percepção também são ruídos, a cada passo em que tocam, com o alcance dos sons do
// alvo vezes o volume da fonte. O som atravessa paredes. Ninguém ouve os próprios ruídos, e um ruído de um alvo só
// chega a quem percebe o grupo dele.
//
// Memória: de cada alvo que viu ou ouviu, o agente lembra onde ele estava da última vez e há quanto tempo foi, até
// passar a memória dele (Perception::memory) sem perceber o alvo de novo. É o que deixa um guarda ir até onde viu o
// jogador sumir atrás da parede. Ruídos sem dono não ficam na memória.
//
// Muitos agentes: os alvos ficam numa grade de células de 16 m, e cada agente só olha os das células que o alcance
// dele cobre. Os raios de linha de visada têm um orçamento por passo (SetRaycastBudget, 128 por padrão): com mais
// pares no cone que isso, os pares olhados há mais tempo ganham o raio (os novos primeiro) e os outros ficam com o
// resultado do último raio deles. Um alvo que entra no cone só é visto quando chega a vez do raio dele.
//
// O que fazer com o que o agente percebe (fugir, perseguir) é do script dele:
//   if (auto* sentidos = GetSystem<motor::PerceptionSystem>()) {
//       for (motor::EntityId alvo : sentidos->GetSeen(GetId())) { /* ... */ }
//       for (const motor::HeardNoise& ruido : sentidos->GetHeard(GetId())) { /* ruido.position */ }
//       if (auto lembra = sentidos->Recall(GetId(), jogador)) { /* lembra->position, lembra->age */ }
//   }

#include "motor/scene/RuntimeSystem.h"
#include "motor/scene/Scene.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace motor {

struct Perception;
struct PerceptionTarget;
class AudioSystem;
class PhysicsSystem;

// Olhos de quem tem Perception, no mundo: a origem da entidade mais a altura dos olhos (para cima no mundo), e a
// frente dela (-Z, girando com a entidade; normalizada).
struct PerceptionEye {
    glm::vec3 position{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
};
PerceptionEye PerceptionEyeOf(const Scene& scene, EntityId id);
// Ponto olhado de um PerceptionTarget, no mundo: a origem mais a altura do ponto olhado.
glm::vec3 PerceptionPointOf(const Scene& scene, EntityId id);

// Um ruído que o agente ouviu no último passo.
struct HeardNoise {
    EntityId source = kNullEntity;  // quem fez (kNullEntity: ruído sem dono, como uma explosão)
    glm::vec3 position{0.0f};       // onde foi feito, no mundo
    float distance = 0.0f;          // dos olhos de quem ouviu até lá
};

// O que um agente lembra de um alvo.
struct PerceptionMemory {
    EntityId target = kNullEntity;
    glm::vec3 position{0.0f};  // onde estava da última vez: a origem dele, se visto; onde fez o ruído, se ouvido
    float age = 0.0f;          // s desde a última vez que foi visto ou ouvido (0: neste passo)
    bool seen = false;         // visto neste passo
    bool heard = false;        // ouvido neste passo
    bool bySight = false;      // a última posição veio da visão (false: de um ruído)
};

// O último passo, para a barra de estatísticas e os testes.
struct PerceptionStats {
    int perceivers = 0;  // entidades com Perception ativas
    int targets = 0;     // Alvos de percepção ativos
    int checks = 0;      // pares (quem olha, alvo) do mesmo grupo, nas células que o alcance cobre
    int raycasts = 0;    // pares no alcance e no cone que ganharam o raio de linha de visada neste passo
    int noises = 0;      // ruídos do passo (de MakeNoise e dos sons dos alvos)
    int deferred = 0;    // pares no cone que ficaram sem raio neste passo (passou do orçamento)
    double ms = 0.0;
};

class PerceptionSystem final : public RuntimeSystem {
public:
    void OnStart(SceneRuntime& runtime) override;
    void OnFixedUpdate(SceneRuntime& runtime, float dt) override;
    void OnStop(SceneRuntime& runtime) override;

    // O que o agente vê agora (no último passo). Vazio se a entidade não tem Perception.
    std::vector<EntityId> GetSeen(EntityId agent) const;
    bool CanSee(EntityId agent, EntityId target) const;

    // Olha agora, sem esperar o passo: o alvo está no alcance, no cone e sem nada no meio? Não guarda nada.
    bool CheckSight(EntityId agent, EntityId target) const;

    // Faz um ruído em `position` que chega até `range` metros (vezes a audição de quem ouve). `source`: quem fez
    // (ninguém ouve os próprios ruídos; vazio = ruído sem dono). Os agentes ouvem no próximo passo fixo.
    void MakeNoise(const glm::vec3& position, float range, EntityId source = kNullEntity);
    // O que o agente ouviu no último passo, do mais perto ao mais longe; de cada fonte, só o ruído mais perto.
    std::vector<HeardNoise> GetHeard(EntityId agent) const;
    bool CanHear(EntityId agent, EntityId source) const;  // ouviu algum ruído de `source` no último passo

    // O que o agente lembra, do mais recente ao mais antigo (no empate, do mais perto ao mais longe).
    std::vector<PerceptionMemory> GetMemory(EntityId agent) const;
    // O que lembra de um alvo; nullopt se nunca o percebeu ou já esqueceu.
    std::optional<PerceptionMemory> Recall(EntityId agent, EntityId target) const;
    // Esquece um alvo (chegou onde o viu e não achou nada) ou, com kNullEntity, tudo. Percebê-lo de novo volta a lembrar.
    void Forget(EntityId agent, EntityId target = kNullEntity);

    // Raios de linha de visada por passo (pelo menos 1). Mais raios: visão mais em dia e passo mais caro.
    void SetRaycastBudget(int raysPerStep);
    int RaycastBudget() const { return m_raycastBudget; }

    const PerceptionStats& Stats() const { return m_stats; }

private:
    struct Sight {
        EntityId target = kNullEntity;
        float distance = 0.0f;
    };
    struct TargetInfo {
        EntityId id = kNullEntity;
        glm::vec3 point{0.0f};  // ponto olhado, no mundo
        const PerceptionTarget* settings = nullptr;
    };
    struct Candidate {  // par no alcance e no cone, esperando a linha de visada
        EntityId agent = kNullEntity;
        int target = 0;  // em m_targets
        float distance = 0.0f;
        glm::vec3 eye{0.0f};
        uint64_t lastRay = 0;  // passo do último raio do par (0: nunca)
        bool clear = false;
    };
    struct LosEntry {
        bool clear = false;
        uint64_t step = 0;  // passo do último raio
        uint64_t used = 0;  // último passo em que o par estava no cone
    };
    static constexpr float kCellSize = 16.0f;
    static constexpr int kDefaultRaycastBudget = 128;
    static int CellOf(float coordinate);
    static uint64_t CellKey(int x, int z);
    static uint64_t PairKey(EntityId agent, EntityId target);
    void GatherNearby(const glm::vec3& center, float reach, std::vector<int>& out) const;
    // Alcance e cone (o barato). `distance` recebe a distância dos olhos ao ponto olhado.
    static bool InView(const Perception& senses, const PerceptionEye& eye, const glm::vec3& point, float visibility,
                       float* distance);
    // Linha de visada (o caro): nada entre os olhos e o ponto olhado, fora o próprio alvo e quem olha.
    bool LineOfSight(EntityId agent, const glm::vec3& eye, EntityId target, const glm::vec3& point,
                     float distance) const;
    struct Noise {
        glm::vec3 position{0.0f};
        float range = 0.0f;
        EntityId source = kNullEntity;
    };
    void CollectSounds(std::vector<Noise>& noises) const;  // os sons tocando nas Fontes de áudio dos alvos
    void Hear(EntityId agent, const Perception& senses, const std::vector<Noise>& noises);
    void Remember(EntityId agent, const Perception& senses, float dt);

    const Scene* m_scene = nullptr;
    const PhysicsSystem* m_physics = nullptr;
    const AudioSystem* m_audio = nullptr;
    std::unordered_map<EntityId, std::vector<Sight>> m_seen;
    std::unordered_map<EntityId, std::vector<HeardNoise>> m_heard;
    std::unordered_map<EntityId, std::vector<PerceptionMemory>> m_memory;
    std::vector<Noise> m_pending;  // de MakeNoise, ouvidos no próximo passo
    std::vector<TargetInfo> m_targets;                      // do passo
    std::unordered_map<uint64_t, std::vector<int>> m_grid;  // célula -> índices em m_targets
    std::unordered_map<uint64_t, LosEntry> m_los;           // (agente, alvo) -> último raio
    int m_raycastBudget = kDefaultRaycastBudget;
    uint64_t m_step = 0;
    PerceptionStats m_stats;
};

} // namespace motor
