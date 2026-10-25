#pragma once

// Ligação de navegação (Fase 35): um atalho entre dois pontos do navmesh que não se ligam andando, como o pulo de
// uma plataforma para outra, a descida de um muro ou uma escada de mão. Só dados: o navmesh do NavSystem
// (motor/ai/NavSystem.h) ganha a ligação quando é gerado, e o agente que a usa vai de uma ponta à outra em arco
// (pulo) ou em linha reta (escada, com `arcHeight` 0).
//
// As pontas são relativas à entidade (mover a entidade leva a ligação junto) e precisam cair a até `radius` do
// navmesh, senão a ligação fica de fora. Mudou uma ligação? Gere o navmesh de novo (Inspector ou Rebuild).

#include "motor/scene/Component.h"

#include <glm/vec3.hpp>

namespace motor {

struct NavMeshLink {
    glm::vec3 start{0.0f, 0.0f, -1.0f};
    glm::vec3 end{0.0f, 0.0f, 1.0f};
    float radius = 0.5f;        // quão longe do navmesh as pontas podem ficar
    bool bidirectional = true;  // desligado: só do início para o fim (descer um muro, por exemplo)
    float arcHeight = 1.0f;     // altura do pulo acima da linha entre as pontas (0 = reto, escada)

    bool operator==(const NavMeshLink&) const = default;
};

} // namespace motor
