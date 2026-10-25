#include "motor/scene/PerceptionTarget.h"

namespace motor {

MOTOR_COMPONENT_MODULE(PerceptionTarget) {
    registry.Register<PerceptionTarget>("PerceptionTarget", "Alvo de percepção")
        .Field("group", "Grupo", &PerceptionTarget::group,
               {.tooltip = "Os agentes escolhem quais grupos percebem (Jogador, Ovelha...)"})
        .Field("height", "Altura do ponto olhado", &PerceptionTarget::height,
               {.speed = 0.01f, .min = -100.0f, .max = 100.0f, .format = "%.2f m",
                .tooltip = "Acima da origem: para onde vai a linha de visada (o meio do corpo)"})
        .Field("visibility", "Visibilidade", &PerceptionTarget::visibility,
               {.speed = 0.01f, .min = 0.0f, .max = 10.0f, .format = "%.2f",
                .tooltip = "Multiplica o alcance da visão de quem olha (agachado, no escuro); 0 = invisível"})
        .Field("soundsAreNoise", "Sons são ouvidos", &PerceptionTarget::soundsAreNoise,
               {.tooltip = "Os sons da Fonte de áudio desta entidade também chegam aos agentes"});
}

} // namespace motor
