#pragma once

#include <motor/scene/GameHost.h>

#include <utility>
#include <vector>

namespace editor {

// GameHost do Play no editor (opções do jogador e sair, pedidos pelos scripts). As opções começam nas padrão do
// projeto a cada Play e não são gravadas; só a qualidade (MSAA, sombras) vale no editor, aplicada pelo
// PlayController no renderer. Tela cheia e resolução ficam guardadas para os scripts lerem de volta.
class PlayHost final : public motor::GameHost {
public:
    void Reset(const motor::GameOptions& defaults);

    motor::GameOptions Options() const override { return m_options; }
    void ApplyOptions(const motor::GameOptions& options) override;
    std::vector<motor::Resolution> Resolutions() const override;
    void QuitGame() override { m_quit = true; }

    bool TakeQuit() { return std::exchange(m_quit, false); }

private:
    motor::GameOptions m_options;
    bool m_quit = false;
    bool m_warnedDisplay = false;
};

} // namespace editor
