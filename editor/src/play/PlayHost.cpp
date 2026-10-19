#include "play/PlayHost.h"

#include <motor/core/Log.h>
#include <motor/core/Window.h>

namespace editor {

using namespace motor;

void PlayHost::Reset(const GameOptions& defaults) {
    m_options = defaults;
    m_options.Clamp();
    m_quit = false;
    m_warnedDisplay = false;
}

void PlayHost::ApplyOptions(const GameOptions& options) {
    const GameOptions before = m_options;
    m_options = options;
    m_options.Clamp();
    log::Info("Opções do jogador (Play): {}, {}x{}, vsync {}, MSAA {}x, sombras {}",
              m_options.fullscreen ? "tela cheia" : "janela", m_options.width, m_options.height,
              m_options.vsync ? "ligado" : "desligado", m_options.msaa, ShadowSettingName(m_options.shadows));
    const bool display = m_options.fullscreen != before.fullscreen || m_options.width != before.width ||
                         m_options.height != before.height || m_options.vsync != before.vsync;
    if (display && !m_warnedDisplay) {
        m_warnedDisplay = true;
        log::Info("No editor, tela cheia, resolução e vsync só valem no jogo exportado (MSAA e sombras valem no Play)");
    }
}

std::vector<Resolution> PlayHost::Resolutions() const {
    std::vector<Resolution> list;
    for (const VideoMode& mode : Window::MonitorModes()) list.push_back({mode.width, mode.height});
    return list;
}

} // namespace editor
