#include "motor/scene/AudioListener.h"

namespace motor {

// Nome do módulo "Sound...": ver AudioSource.cpp (ordem do menu "Adicionar componente").
MOTOR_COMPONENT_MODULE(SoundListener) {
    registry.Register<AudioListener>("AudioListener", "Ouvinte de áudio")
        .Field("volume", "Volume geral", &AudioListener::volume,
               {.slider = true, .min = 0.0f, .max = 1.0f, .tooltip = "Multiplica todos os sons do jogo"});
}

} // namespace motor
