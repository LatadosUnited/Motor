#include "motor/scene/NavMeshAgent.h"

namespace motor {

MOTOR_COMPONENT_MODULE(NavAgent) {
    registry.Register<NavMeshAgent>("NavMeshAgent", "Agente de navegação")
        .Field("speed", "Velocidade", &NavMeshAgent::speed,
               {.speed = 0.05f, .min = 0.0f, .max = 1000.0f, .format = "%.2f m/s"})
        .Field("acceleration", "Aceleração", &NavMeshAgent::acceleration,
               {.speed = 0.1f, .min = 0.01f, .max = 10000.0f, .format = "%.1f m/s²",
                .tooltip = "Quanto demora para chegar à velocidade e para frear"})
        .Field("angularSpeed", "Velocidade de giro", &NavMeshAgent::angularSpeed,
               {.speed = 1.0f, .min = 0.0f, .max = 10000.0f, .format = "%.0f°/s"})
        .Field("stoppingDistance", "Distância de parada", &NavMeshAgent::stoppingDistance,
               {.speed = 0.01f, .min = 0.0f, .max = 1000.0f, .format = "%.2f m", .tooltip = "Para a esta distância do destino"})
        .Field("baseOffset", "Altura da base", &NavMeshAgent::baseOffset,
               {.speed = 0.01f, .min = -100.0f, .max = 100.0f, .format = "%.2f m",
                .tooltip = "Altura da origem da entidade acima do chão (0 = os pés estão na origem)"})
        .Field("updateRotation", "Virar para onde anda", &NavMeshAgent::updateRotation)
        .Field("target", "Alvo", &NavMeshAgent::target,
               {.tooltip = "Segue esta entidade pelo navmesh (vazio: o destino vem de um script)"})
        .Field("repathDistance", "Refazer caminho a cada", &NavMeshAgent::repathDistance,
               {.speed = 0.05f, .min = 0.05f, .max = 1000.0f, .format = "%.2f m",
                .tooltip = "Com alvo: quanto ele precisa andar para o caminho ser refeito"});
}

} // namespace motor
