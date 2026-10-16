// Registro do Projetil (a struct está em Projetil.h). Cada script é registrado uma vez só, num .cpp.

#include "Projetil.h"

MOTOR_SCRIPT(Projetil) {
    registry.Register<Projetil>("Projetil", "Projétil")
        .Field("velocidade", "Velocidade", &Projetil::velocidade)
        .Field("vida", "Vida", &Projetil::vida, {.speed = 0.1f, .min = 0.0f, .max = 60.0f, .format = "%.1f s"});
}
