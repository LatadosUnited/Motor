#include "motor/scene/NavMeshLink.h"

namespace motor {

MOTOR_COMPONENT_MODULE(NavLink) {
    registry.Register<NavMeshLink>("NavMeshLink", "Ligação de navegação")
        .Field("start", "Início", &NavMeshLink::start,
               {.speed = 0.01f, .tooltip = "Ponta de saída, relativa à entidade"})
        .Field("end", "Fim", &NavMeshLink::end, {.speed = 0.01f, .tooltip = "Ponta de chegada, relativa à entidade"})
        .Field("radius", "Raio", &NavMeshLink::radius,
               {.speed = 0.01f, .min = 0.05f, .max = 100.0f, .format = "%.2f m",
                .tooltip = "Quão longe do navmesh as pontas podem ficar"})
        .Field("bidirectional", "Nos dois sentidos", &NavMeshLink::bidirectional,
               {.tooltip = "Desligado: só do início para o fim"})
        .Field("arcHeight", "Altura do arco", &NavMeshLink::arcHeight,
               {.speed = 0.01f, .min = 0.0f, .max = 100.0f, .format = "%.2f m",
                .tooltip = "Pulo em arco acima da linha entre as pontas (0 = reto, como uma escada)"});
}

} // namespace motor
