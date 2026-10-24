#include "motor/project/InputActionTracker.h"

#include <algorithm>
#include <cctype>

namespace motor {

namespace {

bool SameName(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

} // namespace

InputActionTracker::Track& InputActionTracker::Get(std::string_view action, int device) {
    for (Track& t : m_tracks) {
        if (t.device == device && SameName(t.action, action)) return t;
    }
    Track& t = m_tracks.emplace_back();
    t.action = std::string(action);
    t.device = device;
    return t;
}

void InputActionTracker::Step(Track& t, bool down, float dt) {
    // Começando agora: o que já estava apertado não vira um aperto (nem toque, nem buffer).
    if (t.fresh) {
        t.fresh = false;
        t.down = down;
        t.pressed = t.released = t.doubleTap = false;
        return;
    }
    t.pressed = down && !t.down;
    t.released = !down && t.down;
    t.prevHeld = t.held;
    if (t.pressed) {
        t.held = 0.0f;
    } else if (down) {
        t.held += dt;
    } else if (!t.released) {
        t.held = 0.0f;  // no quadro do released fica o tempo apertada, para o toque
    }
    t.sincePress += dt;
    t.doubleTap = false;
    if (t.pressed) {
        t.pressGap = t.sincePress;
        t.sincePress = 0.0f;
        // Pode fechar uma dupla se o aperto anterior não fechou outra; o intervalo é conferido na consulta.
        t.doubleTap = !t.pairedPress;
        t.pairedPress = false;
        t.bufferUsed = false;
    }
    t.down = down;
}

void InputActionTracker::Update(const InputActions& actions, const InputFilter& filter, bool enabled, float dt) {
    for (const InputAction& action : actions.actions) Get(action.name, kAnyDevice);
    for (Track& t : m_tracks) {
        InputFilter f = filter;
        f.device = t.device;
        // Ação que saiu do mapa: fica solta (sem avisar a cada quadro).
        const bool known = actions.Find(t.action) != nullptr;
        Step(t, enabled && known && actions.Value(t.action, f) > 0.5f, dt);
    }
}

float InputActionTracker::HeldTime(std::string_view action, int device) {
    const Track& t = Get(action, device);
    return t.down ? t.held : 0.0f;
}

bool InputActionTracker::WasHeld(std::string_view action, float seconds, int device) {
    const Track& t = Get(action, device);
    return t.down && !t.pressed && t.prevHeld < seconds && t.held >= seconds;
}

bool InputActionTracker::WasTapped(std::string_view action, float maxSeconds, int device) {
    const Track& t = Get(action, device);
    return t.released && t.held <= maxSeconds;
}

bool InputActionTracker::WasDoubleTapped(std::string_view action, float interval, int device) {
    Track& t = Get(action, device);
    if (!t.pressed || !t.doubleTap || t.pressGap > interval) return false;
    t.pairedPress = true;  // o próximo aperto começa outra dupla
    return true;
}

bool InputActionTracker::ConsumeBuffered(std::string_view action, float seconds, int device) {
    Track& t = Get(action, device);
    if (t.bufferUsed || t.sincePress > seconds) return false;
    t.bufferUsed = true;
    return true;
}

} // namespace motor
