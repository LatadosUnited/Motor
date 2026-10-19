#pragma once

#include "motor/project/GameOptions.h"

#include <string>
#include <vector>

namespace motor {

// Quem roda o jogo (o Player do jogo exportado, o Play do editor), visto pelos scripts: opções do jogador e
// sair do jogo. O SceneRuntime só guarda o ponteiro (SetHost); sem host (testes), as opções são as padrão e
// mudar ou sair não faz nada.
class GameHost {
public:
    virtual ~GameHost() = default;

    virtual GameOptions Options() const = 0;
    // O Player aplica na hora (janela, vsync, MSAA, sombras) e grava em %APPDATA%\<Jogo>\opcoes.json. O editor
    // aplica só a qualidade no Play e não grava: tela cheia e resolução são do jogo exportado.
    virtual void ApplyOptions(const GameOptions& options) = 0;
    // Resoluções de tela cheia do monitor principal, da menor para a maior.
    virtual std::vector<Resolution> Resolutions() const = 0;
    // Versão do jogo (Configurações do jogo), para mostrar no menu.
    virtual std::string GameVersion() const = 0;
    // Fecha o jogo depois deste passo (no editor, para o Play).
    virtual void QuitGame() = 0;
};

} // namespace motor
