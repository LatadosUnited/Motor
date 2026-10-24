#include "motor/scene/NavMeshSurface.h"

namespace motor {

// "Nav..." deixa os dois componentes de navegação juntos no menu "Adicionar componente".
MOTOR_COMPONENT_MODULE(NavSurface) {
    registry.Register<NavMeshSurface>("NavMeshSurface", "Superfície de navegação")
        .Field("agentRadius", "Raio do agente", &NavMeshSurface::agentRadius,
               {.speed = 0.01f, .min = 0.05f, .max = 50.0f, .format = "%.2f m",
                .tooltip = "Distância que o navmesh mantém das paredes"})
        .Field("agentHeight", "Altura do agente", &NavMeshSurface::agentHeight,
               {.speed = 0.01f, .min = 0.1f, .max = 100.0f, .format = "%.2f m",
                .tooltip = "Vão livre mínimo: por baixo de algo mais baixo o agente não passa"})
        .Field("agentMaxClimb", "Altura do degrau", &NavMeshSurface::agentMaxClimb,
               {.speed = 0.01f, .min = 0.0f, .max = 10.0f, .format = "%.2f m", .tooltip = "Degrau mais alto que o agente sobe"})
        .Field("agentMaxSlope", "Inclinação máxima", &NavMeshSurface::agentMaxSlope,
               {.slider = true, .min = 0.0f, .max = 89.0f, .format = "%.0f°", .tooltip = "Rampa mais íngreme que o agente sobe"})
        .Field("cellSize", "Tamanho da célula", &NavMeshSurface::cellSize,
               {.speed = 0.005f, .min = 0.02f, .max = 5.0f, .format = "%.3f m",
                .tooltip = "Menor = navmesh mais fiel às bordas e geração mais lenta. Bom valor: metade do raio"})
        .Field("cellHeight", "Altura da célula", &NavMeshSurface::cellHeight,
               {.speed = 0.005f, .min = 0.02f, .max = 5.0f, .format = "%.3f m", .tooltip = "Bom valor: metade do tamanho da célula"})
        .Field("minRegionArea", "Área mínima", &NavMeshSurface::minRegionArea,
               {.speed = 0.1f, .min = 0.0f, .max = 1000.0f, .format = "%.1f m²",
                .tooltip = "Pedaços de chão menores que isso somem (topo de mesa, de caixa)"});
}

} // namespace motor
