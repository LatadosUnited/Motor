#pragma once

#include "motor/scene/Scene.h"

#include <string_view>

namespace motor {

// Tela de carregamento (Fase 21): o jogo exportado desenha uma cena parada (sem scripts nem física) enquanto abre a
// cena inicial e a cada LoadScene. A do projeto (Configurações do jogo > Tela de carregamento) é uma cena comum,
// feita no editor com UI (imagem, texto) e, se quiser, uma câmera e objetos 3D. Sem uma, vale esta: fundo escuro
// e o texto embaixo à direita.
Scene DefaultLoadingScene(std::string_view text = "Carregando...");

} // namespace motor
