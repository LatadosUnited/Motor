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
// O que fazer com o que o agente percebe (fugir, perseguir) é do script dele:
//   if (auto* sentidos = GetSystem<motor::PerceptionSystem>()) {
//       for (motor::EntityId alvo : sentidos->GetSeen(GetId())) { /* ... */ }
//       for (const motor::HeardNoise& ruido : sentidos->GetHeard(GetId())) { /* ruido.position */ }
//   }

#include "motor/scene/RuntimeSystem.h"
#include "motor/scene/Scene.h"

#include <glm/vec3.hpp>

#include <unordered_map>
#include <vector>

namespace motor {

struct Perception;
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

// O último passo, para a barra de estatísticas e os testes.
struct PerceptionStats {
    int perceivers = 0;  // entidades com Perception ativas
    int targets = 0;     // Alvos de percepção ativos
    int checks = 0;      // pares (quem olha, alvo) do mesmo grupo
    int raycasts = 0;    // pares que passaram pelo alcance e pelo cone e foram até a linha de visada
    int noises = 0;      // ruídos do passo (de MakeNoise e dos sons dos alvos)
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

    const PerceptionStats& Stats() const { return m_stats; }

private:
    struct Sight {
        EntityId target = kNullEntity;
        float distance = 0.0f;
    };
    // Alcance e cone (o barato). `distance` recebe a distância dos olhos ao ponto olhado.
    bool InView(EntityId agent, EntityId target, float* distance) const;
    // Linha de visada (o caro): nada entre os olhos e o ponto olhado, fora o próprio alvo e quem olha.
    bool LineOfSight(EntityId agent, EntityId target, float distance) const;
    struct Noise {
        glm::vec3 position{0.0f};
        float range = 0.0f;
        EntityId source = kNullEntity;
    };
    void CollectSounds(std::vector<Noise>& noises) const;  // os sons tocando nas Fontes de áudio dos alvos
    void Hear(EntityId agent, const Perception& senses, const std::vector<Noise>& noises);

    const Scene* m_scene = nullptr;
    const PhysicsSystem* m_physics = nullptr;
    const AudioSystem* m_audio = nullptr;
    std::unordered_map<EntityId, std::vector<Sight>> m_seen;
    std::unordered_map<EntityId, std::vector<HeardNoise>> m_heard;
    std::vector<Noise> m_pending;  // de MakeNoise, ouvidos no próximo passo
    PerceptionStats m_stats;
};

} // namespace motor
