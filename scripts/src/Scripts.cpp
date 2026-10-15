#include "scripts/Scripts.h"

#include <motor/scene/Component.h>

namespace scripts {

void RegisterAll(motor::ComponentRegistry& registry) {
    if (registry.Find("Girar")) return;  // já registrados

    registry.Register<Girar>("Girar", "Girar")
        .Field("eixo", "Eixo", &Girar::eixo)
        .Field("velocidade", "Velocidade", &Girar::velocidade,
               {.speed = 1.0f, .format = "%.0f°/s", .tooltip = "Graus por segundo (negativo gira ao contrário)"})
        .Field("local", "Eixo local", &Girar::local, {.tooltip = "Gira no eixo do próprio objeto, não do mundo"});

    registry.Register<MoverWasd>("MoverWASD", "Mover com WASD")
        .Field("velocidade", "Velocidade", &MoverWasd::velocidade,
               {.speed = 0.1f, .min = 0.0f, .max = 100.0f, .format = "%.1f m/s"})
        .Field("corrida", "Corrida", &MoverWasd::corrida,
               {.speed = 0.05f, .min = 1.0f, .max = 10.0f, .format = "x%.1f", .tooltip = "Multiplicador com Shift"})
        .Field("virar", "Virar", &MoverWasd::virar, {.tooltip = "Vira o objeto para a direção do movimento"});

    registry.Register<Seguir>("Seguir", "Seguir")
        .Field("alvo", "Alvo", &Seguir::alvo, {.tooltip = "Nome da entidade a seguir"})
        .Field("deslocamento", "Deslocamento", &Seguir::deslocamento)
        .Field("suavidade", "Suavidade", &Seguir::suavidade,
               {.speed = 0.1f, .min = 0.0f, .max = 50.0f, .tooltip = "0 = cola no alvo"})
        .Field("olhar", "Olhar o alvo", &Seguir::olhar);
}

} // namespace scripts
