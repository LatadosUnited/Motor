#include "motor/scene/NavMeshAgentAnimation.h"

namespace motor {

MOTOR_COMPONENT_MODULE(NavAgentAnimation) {
    registry.Register<NavMeshAgentAnimation>("NavMeshAgentAnimation", "Animação do agente")
        .Field("idleClip", "Clipe parado", &NavMeshAgentAnimation::idleClip,
               {.tooltip = "Nome do clipe do Animator (na entidade ou num filho); vazio: não troca"})
        .Field("walkClip", "Clipe andando", &NavMeshAgentAnimation::walkClip)
        .Field("runClip", "Clipe correndo", &NavMeshAgentAnimation::runClip, {.tooltip = "Vazio: só anda"})
        .Field("jumpClip", "Clipe pulando", &NavMeshAgentAnimation::jumpClip,
               {.tooltip = "Enquanto atravessa uma Ligação de navegação (vazio: anda)"})
        .Field("walkSpeed", "Velocidade do andar", &NavMeshAgentAnimation::walkSpeed,
               {.speed = 0.05f, .min = 0.01f, .max = 100.0f, .format = "%.2f m/s",
                .tooltip = "Em que o clipe de andar toca na velocidade normal"})
        .Field("runSpeed", "Velocidade da corrida", &NavMeshAgentAnimation::runSpeed,
               {.speed = 0.05f, .min = 0.01f, .max = 100.0f, .format = "%.2f m/s",
                .tooltip = "Em que o clipe de correr toca na velocidade normal; corre da metade entre as duas para cima"})
        .Field("idleBelow", "Parado abaixo de", &NavMeshAgentAnimation::idleBelow,
               {.speed = 0.01f, .min = 0.0f, .max = 10.0f, .format = "%.2f m/s"})
        .Field("matchSpeed", "Acompanhar a velocidade", &NavMeshAgentAnimation::matchSpeed,
               {.tooltip = "O Animator toca mais rápido ou mais devagar conforme o agente (os pés não escorregam)"})
        .Field("fade", "Mistura", &NavMeshAgentAnimation::fade,
               {.speed = 0.01f, .min = 0.0f, .max = 5.0f, .format = "%.2f s"});
}

} // namespace motor
