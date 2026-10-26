#include "motor/scene/Perception.h"

namespace motor {

MOTOR_COMPONENT_MODULE(Perception) {
    registry.Register<Perception>("Perception", "Percepção")
        .Field("sightRange", "Alcance da visão", &Perception::sightRange,
               {.speed = 0.1f, .min = 0.0f, .max = 10000.0f, .format = "%.1f m",
                .tooltip = "Até onde vê (vezes a visibilidade do alvo); 0 = cego"})
        .Field("sightAngle", "Ângulo da visão", &Perception::sightAngle,
               {.speed = 0.5f, .min = 0.0f, .max = 360.0f, .format = "%.0f°",
                .tooltip = "Abertura inteira do cone de visão, para a frente da entidade"})
        .Field("nearRange", "Percebe em volta até", &Perception::nearRange,
               {.speed = 0.05f, .min = 0.0f, .max = 1000.0f, .format = "%.2f m",
                .tooltip = "Perto assim percebe em qualquer direção, atrás também (se nada tapar)"})
        .Field("eyeHeight", "Altura dos olhos", &Perception::eyeHeight,
               {.speed = 0.01f, .min = -100.0f, .max = 100.0f, .format = "%.2f m",
                .tooltip = "Acima da origem da entidade"})
        .Field("hearing", "Audição", &Perception::hearing,
               {.speed = 0.01f, .min = 0.0f, .max = 100.0f, .format = "%.2f",
                .tooltip = "Multiplica o alcance dos ruídos que chegam a ele; 0 = surdo"})
        .Field("memory", "Memória", &Perception::memory,
               {.speed = 0.1f, .min = 0.0f, .max = 10000.0f, .format = "%.1f s",
                .tooltip = "Quanto tempo lembra de um alvo que não percebe mais"})
        .Field("detects", "Percebe os grupos", &Perception::detects,
               {.tooltip = "Grupos dos Alvos de percepção, separados por vírgula (vazio: todos)"});
}

} // namespace motor
