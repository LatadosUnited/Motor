#pragma once

// Ouvinte dos sons 3D (Fase 8): normalmente na câmera. Os sons 3D são ouvidos da posição e da direção desta
// entidade. Sem nenhum AudioListener ativo, o ouvinte é a câmera principal (e, sem câmera, a origem).

#include "motor/scene/Component.h"

namespace motor {

struct AudioListener {
    float volume = 1.0f;  // volume geral do jogo (multiplica todos os sons)
};

} // namespace motor
