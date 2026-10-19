#pragma once

#include "motor/project/GameOptions.h"
#include "motor/renderer/Renderer.h"

#include <algorithm>

namespace motor {

// Qualidade das opções do jogador no renderer: MSAA e sombras (Baixas/Médias/Altas limitam a qualidade de cada
// luz; Desligadas tira todas). Usado pelo Player e pelo Play do editor.
inline void ApplyGameQuality(Renderer& renderer, const GameOptions& options) {
    renderer.SetSamples(options.msaa);
    renderer.SetShadows(options.shadows != ShadowSetting::Off,
                        static_cast<ShadowQuality>(std::max(0, static_cast<int>(options.shadows) - 1)));
}

} // namespace motor
