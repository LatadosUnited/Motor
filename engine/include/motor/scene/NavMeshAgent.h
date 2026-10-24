#pragma once

// Agente de navegação (Fase 34): uma entidade que anda pelo navmesh até um destino, desviando das paredes pelo
// caminho que o Detour acha. Só dados: quem move é o NavSystem (motor/ai/NavSystem.h), antes da física e dos
// scripts. O destino vem do campo `target` (segue a entidade) ou de um script (NavSystem::SetDestination).
//
// O agente mexe no Transform direto (a origem fica `baseOffset` acima do navmesh). Com corpo físico (Rigidbody,
// CharacterController) o encaixe com a física é a Fase 35; até lá, use o agente em entidades sem física.

#include "motor/scene/Component.h"
#include "motor/scene/EntityRef.h"

namespace motor {

struct NavMeshAgent {
    float speed = 3.5f;             // m/s
    float acceleration = 8.0f;      // m/s²: quanto demora para chegar à velocidade e para frear
    float angularSpeed = 360.0f;    // graus/s ao virar para onde anda
    float stoppingDistance = 0.1f;  // chega a esta distância do destino e para
    float baseOffset = 0.0f;        // altura da origem da entidade acima do navmesh (pés na origem = 0)
    bool updateRotation = true;     // vira a entidade (só em Y) para onde anda
    EntityRef target;               // segue esta entidade (vazio: só o destino dado por script)
    float repathDistance = 0.5f;    // o alvo andou mais que isso desde o último caminho: refaz
    // Multidão (Fase 35): o tamanho com que os outros agentes desviam deste (o navmesh continua com o raio da
    // superfície; até 2 m). Sem desviar, o agente vai reto, mas ainda não atravessa os outros.
    float radius = 0.5f;
    float height = 2.0f;
    bool avoidOthers = true;

    bool operator==(const NavMeshAgent&) const = default;
};

} // namespace motor
