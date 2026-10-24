#pragma once

#include "motor/project/InputActions.h"

#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

// Histórico das ações no tempo, para o que um quadro só não diz: segurar por um tempo, toque curto, toque duplo e
// buffer (apertou um pouco antes da hora: o pulo apertado no ar vale ao tocar o chão).
//
// Quem roda o jogo chama Update uma vez por quadro, antes dos scripts. Todas as ações do mapa são acompanhadas
// com o dispositivo kAnyDevice desde o começo; um jogador específico (device 0..3, kKeyboardMouse) passa a ser
// acompanhado na primeira consulta, e dali em diante a cada quadro.
class InputActionTracker {
public:
    // `filter.device` é ignorado (cada entrada tem o seu); valem os contextos e o mouse da UI. `enabled` falso
    // (jogo sem foco) lê tudo solto.
    void Update(const InputActions& actions, const InputFilter& filter, bool enabled, float dt);
    void Clear() { m_tracks.clear(); }

    // Segundos com a ação apertada até agora; 0 se solta.
    float HeldTime(std::string_view action, int device = kAnyDevice);
    // Neste quadro a ação completou `seconds` apertada (uma vez por aperto).
    bool WasHeld(std::string_view action, float seconds, int device = kAnyDevice);
    // Soltou neste quadro depois de ficar apertada no máximo `maxSeconds`.
    bool WasTapped(std::string_view action, float maxSeconds, int device = kAnyDevice);
    // Apertou neste quadro e o aperto anterior foi há no máximo `interval` segundos. Um terceiro aperto rápido
    // começa outra dupla (não conta de novo com o segundo).
    bool WasDoubleTapped(std::string_view action, float interval, int device = kAnyDevice);
    // Apertou nos últimos `seconds` e ninguém usou esse aperto ainda: devolve true uma vez e o gasta.
    bool ConsumeBuffered(std::string_view action, float seconds, int device = kAnyDevice);

private:
    static constexpr float kNever = std::numeric_limits<float>::infinity();
    struct Track {
        std::string action;  // nome como está no mapa
        int device = kAnyDevice;
        bool down = false;
        bool pressed = false;   // neste quadro
        bool released = false;  // neste quadro
        float held = 0.0f;      // segundos apertada (até agora, ou até soltar no quadro do released)
        float prevHeld = 0.0f;  // o `held` do quadro anterior
        float sincePress = kNever;    // desde o último aperto
        float pressGap = kNever;      // entre o aperto deste quadro e o anterior
        bool pairedPress = false;     // o último aperto já fechou uma dupla
        bool doubleTap = false;       // neste quadro
        bool bufferUsed = true;       // o último aperto já foi gasto
        bool fresh = true;            // começou a ser acompanhada agora: sem histórico ainda
    };
    Track& Get(std::string_view action, int device);
    void Step(Track& t, bool down, float dt);

    std::vector<Track> m_tracks;
};

} // namespace motor
