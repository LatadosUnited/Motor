// Registro do Placar (a struct está em Placar.h). "pontos" é registrado para aparecer no Inspector, e lá
// dá para ver o valor mudar durante o Play.

#include "Placar.h"

MOTOR_SCRIPT(Placar) {
    registry.Register<Placar>("Placar", "Placar")
        .Field("pontos", "Pontos", &Placar::pontos, {.tooltip = "Começa com este valor no Play"})
        .Field("mensagemFinal", "Mensagem final", &Placar::mensagemFinal);
}
