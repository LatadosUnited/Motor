#include "motor/core/Input.h"

#include "motor/core/Log.h"
#include "motor/common/Text.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <utility>

namespace motor {

namespace {
constexpr int kMaxKeys = GLFW_KEY_LAST + 1;
constexpr int kMaxButtons = GLFW_MOUSE_BUTTON_LAST + 1;

// Botões com eventos: o estado no fim do quadro e o que aconteceu durante ele. `held` é o que o quadro anterior
// viu (estado ou toque), para o "apertou neste frame" das ações.
template <size_t N>
struct Buttons {
    std::array<bool, N> down{};
    std::array<bool, N> pressed{};
    std::array<bool, N> released{};
    std::array<bool, N> prevHeld{};

    bool Held(int i) const { return down[i] || pressed[i]; }
    void Set(int i, bool isDown) {
        if (down[i] == isDown) return;
        down[i] = isDown;
        (isDown ? pressed : released)[i] = true;
    }
    void ReleaseAll() {
        for (size_t i = 0; i < N; ++i) Set(static_cast<int>(i), false);
    }
    void BeginFrame() {
        for (size_t i = 0; i < N; ++i) prevHeld[i] = Held(static_cast<int>(i));
        pressed.fill(false);
        released.fill(false);
    }
};

struct State {
    Buttons<kMaxKeys> keys;
    Buttons<kMaxButtons> buttons;
    glm::vec2 mousePos{0.0f};
    glm::vec2 prevMousePos{0.0f};
    float scroll = 0.0f;
    float prevScroll = 0.0f;
    glm::vec2 prevVelocity{0.0f};
    float frameTime = 1.0f / 60.0f;
    std::chrono::steady_clock::time_point frameStart = std::chrono::steady_clock::now();
    bool hasMousePos = false;
    bool ignoreDeltaRequested = false;
    bool ignoreDeltaThisFrame = false;
    std::string text;
    uint64_t events = 0;  // EventCount
    InputDevice lastDevice = InputDevice::KeyboardMouse;
    int lastGamepad = -1;
    // Controles, um por vaga
    struct Pad {
        bool connected = false;
        int jid = -1;  // joystick da GLFW; -1 no simulado
        std::string name;
        std::array<bool, kGamepadButtonCount> buttons{};
        std::array<bool, kGamepadButtonCount> prevButtons{};
        std::array<float, kGamepadAxisCount> axes{};
        std::array<float, kGamepadAxisCount> prevAxes{};
        bool justConnected = false;
        bool justDisconnected = false;
    };
    std::array<Pad, kMaxGamepads> pads;
    bool padSimulated = false;
    float deadZone = Input::kGamepadDeadZone;
} g;

bool ValidPadButton(int b) { return b >= 0 && b < kGamepadButtonCount; }
bool ValidPadAxis(int a) { return a >= 0 && a < kGamepadAxisCount; }
bool ValidPad(int pad) { return pad >= 0 && pad < kMaxGamepads; }

void UseKeyboardMouse() { g.lastDevice = InputDevice::KeyboardMouse; }

// Botão apertado ou analógico empurrado mais da metade: o controle passa a ser o último usado.
void NoteGamepadUse(int pad) {
    const State::Pad& p = g.pads[static_cast<size_t>(pad)];
    bool used = false;
    for (int b = 0; b < kGamepadButtonCount; ++b) used = used || (p.buttons[b] && !p.prevButtons[b]);
    for (int a = 0; a < kGamepadAxisCount; ++a) used = used || std::abs(p.axes[a]) > 0.5f;
    if (used) {
        g.lastDevice = InputDevice::Gamepad;
        g.lastGamepad = pad;
    }
}

void ConnectPad(int pad, int jid, std::string name) {
    State::Pad& p = g.pads[static_cast<size_t>(pad)];
    p = State::Pad{};
    p.connected = true;
    p.jid = jid;
    p.name = std::move(name);
    p.justConnected = true;
    log::Info("Controle {} conectado: {}", pad + 1, p.name);
}

void DisconnectPad(int pad) {
    State::Pad& p = g.pads[static_cast<size_t>(pad)];
    if (!p.connected) return;
    log::Info("Controle {} desconectado: {}", pad + 1, p.name);
    // O que estava apertado no quadro anterior solta neste (WasGamepadButtonReleased vale).
    const auto prevButtons = p.prevButtons;
    const auto prevAxes = p.prevAxes;
    p = State::Pad{};
    p.justDisconnected = true;
    p.prevButtons = prevButtons;
    p.prevAxes = prevAxes;
    if (g.lastGamepad == pad) g.lastGamepad = -1;
}

void AppendUtf8(std::string& out, unsigned int cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        if (cp >= 0xD800 && cp <= 0xDFFF) return;  // metade de um par UTF-16: não é caractere
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

constexpr const char* kPadButtonNames[kGamepadButtonCount] = {
    "A", "B", "X", "Y", "LeftBumper", "RightBumper", "Back", "Start", "Guide", "LeftThumb", "RightThumb",
    "DpadUp", "DpadRight", "DpadDown", "DpadLeft"};
constexpr const char* kPadAxisNames[kGamepadAxisCount] = {"LeftX", "LeftY", "RightX", "RightY", "LeftTrigger",
                                                         "RightTrigger"};
constexpr const char* kMouseNames[kMouseButtonCount] = {"MouseLeft", "MouseRight", "MouseMiddle", "MouseBack",
                                                        "MouseForward"};

struct NamedKey {
    Key key;
    const char* name;
};
constexpr NamedKey kNamedKeys[] = {
    {Key::Space, "Space"}, {Key::Apostrophe, "Apostrophe"}, {Key::Comma, "Comma"}, {Key::Minus, "Minus"},
    {Key::Period, "Period"}, {Key::Slash, "Slash"}, {Key::Semicolon, "Semicolon"}, {Key::Equal, "Equal"},
    {Key::LeftBracket, "LeftBracket"}, {Key::Backslash, "Backslash"}, {Key::RightBracket, "RightBracket"},
    {Key::GraveAccent, "GraveAccent"}, {Key::World1, "World1"}, {Key::World2, "World2"},
    {Key::Escape, "Escape"}, {Key::Enter, "Enter"}, {Key::Tab, "Tab"}, {Key::Backspace, "Backspace"},
    {Key::Insert, "Insert"}, {Key::Delete, "Delete"}, {Key::Right, "Right"}, {Key::Left, "Left"},
    {Key::Down, "Down"}, {Key::Up, "Up"}, {Key::PageUp, "PageUp"}, {Key::PageDown, "PageDown"},
    {Key::Home, "Home"}, {Key::End, "End"}, {Key::CapsLock, "CapsLock"}, {Key::ScrollLock, "ScrollLock"},
    {Key::NumLock, "NumLock"}, {Key::PrintScreen, "PrintScreen"}, {Key::Pause, "Pause"},
    {Key::NumpadDecimal, "NumpadDecimal"}, {Key::NumpadDivide, "NumpadDivide"},
    {Key::NumpadMultiply, "NumpadMultiply"}, {Key::NumpadSubtract, "NumpadSubtract"}, {Key::NumpadAdd, "NumpadAdd"},
    {Key::NumpadEnter, "NumpadEnter"}, {Key::NumpadEqual, "NumpadEqual"},
    {Key::LeftShift, "LeftShift"}, {Key::LeftControl, "LeftControl"},
    {Key::LeftAlt, "LeftAlt"}, {Key::LeftSuper, "LeftSuper"}, {Key::RightShift, "RightShift"},
    {Key::RightControl, "RightControl"}, {Key::RightAlt, "RightAlt"}, {Key::RightSuper, "RightSuper"},
    {Key::Menu, "Menu"}};

bool ValidKey(int k) { return k >= 0 && k < kMaxKeys; }
bool ValidButton(int b) { return b >= 0 && b < kMaxButtons; }

// Nome de cada código, montado uma vez: letras, dígitos, F1 a F24, teclado numérico e as da lista.
const std::array<std::string, kMaxKeys>& KeyNames() {
    static const std::array<std::string, kMaxKeys> names = [] {
        std::array<std::string, kMaxKeys> t;
        for (int i = 0; i < 26; ++i) t[int(Key::A) + i] = std::string(1, char('A' + i));
        for (int i = 0; i < 10; ++i) t[int(Key::D0) + i] = std::string(1, char('0' + i));
        for (int i = 0; i < 24; ++i) t[int(Key::F1) + i] = "F" + std::to_string(i + 1);
        for (int i = 0; i < 10; ++i) t[int(Key::Numpad0) + i] = "Numpad" + std::to_string(i);
        for (const NamedKey& named : kNamedKeys) t[int(named.key)] = named.name;
        return t;
    }();
    return names;
}

void OnKey(GLFWwindow*, int key, int, int action, int) {
    ++g.events;  // até a repetição: segurar uma tecla mantém o editor acordado
    if (!ValidKey(key) || action == GLFW_REPEAT) return;
    if (action == GLFW_PRESS) UseKeyboardMouse();
    g.keys.Set(key, action == GLFW_PRESS);
}

void OnChar(GLFWwindow*, unsigned int codepoint) {
    ++g.events;
    AppendUtf8(g.text, codepoint);
}

void OnMouseButton(GLFWwindow*, int button, int action, int) {
    ++g.events;
    if (!ValidButton(button)) return;
    if (action == GLFW_PRESS) UseKeyboardMouse();
    g.buttons.Set(button, action == GLFW_PRESS);
}

void OnCursorPos(GLFWwindow*, double x, double y) {
    ++g.events;
    const glm::vec2 pos{static_cast<float>(x), static_cast<float>(y)};
    // Só um movimento de verdade troca o dispositivo (esbarrar no mouse jogando de controle, não).
    if (g.hasMousePos && std::abs(pos.x - g.mousePos.x) + std::abs(pos.y - g.mousePos.y) > 4.0f) UseKeyboardMouse();
    g.mousePos = pos;
    if (!g.hasMousePos) {
        g.prevMousePos = g.mousePos;
        g.hasMousePos = true;
    }
}

void OnScroll(GLFWwindow*, double, double yOffset) {
    ++g.events;
    UseKeyboardMouse();
    g.scroll += static_cast<float>(yOffset);
}

void OnFocus(GLFWwindow*, int focused) {
    ++g.events;
    // Evita teclas "presas" quando a janela perde o foco: soltam (o jogo vê o WasKeyReleased).
    if (!focused) {
        g.keys.ReleaseAll();
        g.buttons.ReleaseAll();
    }
}
} // namespace

glm::vec2 ShapeStick(glm::vec2 raw, float deadZone) {
    const float magnitude = std::sqrt(raw.x * raw.x + raw.y * raw.y);
    if (magnitude <= deadZone || magnitude <= 0.0f) return glm::vec2{0.0f};
    const float scaled = std::min((magnitude - deadZone) / (1.0f - deadZone), 1.0f);
    return raw * (scaled / magnitude);
}

uint64_t Input::EventCount() {
    return g.events;
}

bool Input::IsAnyHeld() {
    for (int i = 0; i < kMaxKeys; ++i) {
        if (g.keys.Held(i)) return true;
    }
    for (int i = 0; i < kMaxButtons; ++i) {
        if (g.buttons.Held(i)) return true;
    }
    return false;
}

void Input::Attach(GLFWwindow* window) {
    glfwSetKeyCallback(window, OnKey);
    glfwSetCharCallback(window, OnChar);
    glfwSetMouseButtonCallback(window, OnMouseButton);
    glfwSetCursorPosCallback(window, OnCursorPos);
    glfwSetScrollCallback(window, OnScroll);
    glfwSetWindowFocusCallback(window, OnFocus);
}

void Input::BeginFrame(float frameSeconds) {
    g.prevVelocity = MouseVelocity();
    g.prevScroll = g.scroll;
    const auto now = std::chrono::steady_clock::now();
    const float measured = std::chrono::duration<float>(now - g.frameStart).count();
    g.frameStart = now;
    // Limitado como o dt da Application: um travamento não vira uma velocidade quase zero, nem dois quadros
    // seguidos (testes) uma velocidade infinita.
    g.frameTime = std::clamp(frameSeconds > 0.0f ? frameSeconds : measured, 1e-4f, 0.25f);
    g.keys.BeginFrame();
    g.buttons.BeginFrame();
    for (State::Pad& p : g.pads) {
        p.prevButtons = p.buttons;
        p.prevAxes = p.axes;
        p.justConnected = false;
        p.justDisconnected = false;
    }
    g.prevMousePos = g.mousePos;
    g.scroll = 0.0f;
    g.text.clear();
    g.ignoreDeltaThisFrame = g.ignoreDeltaRequested;
    g.ignoreDeltaRequested = false;
}

void Input::IgnoreNextMouseDelta() {
    g.ignoreDeltaRequested = true;
}

void Input::SetKeyState(Key key, bool down) {
    const int k = static_cast<int>(key);
    if (!ValidKey(k)) return;
    if (down) UseKeyboardMouse();
    g.keys.Set(k, down);
}

void Input::SetMouseButtonState(MouseButton button, bool down) {
    const int b = static_cast<int>(button);
    if (!ValidButton(b)) return;
    if (down) UseKeyboardMouse();
    g.buttons.Set(b, down);
}

void Input::AddScroll(float notches) {
    OnScroll(nullptr, 0.0, notches);
}

void Input::AddTextInput(std::string_view utf8) {
    g.text += utf8;
}

const std::string& Input::TextInput() {
    return g.text;
}

bool Input::IsKeyDown(Key key) {
    const int k = static_cast<int>(key);
    return ValidKey(k) && g.keys.Held(k);
}

bool Input::WasKeyPressed(Key key) {
    const int k = static_cast<int>(key);
    return ValidKey(k) && g.keys.pressed[k];
}

bool Input::WasKeyReleased(Key key) {
    const int k = static_cast<int>(key);
    return ValidKey(k) && g.keys.released[k];
}

bool Input::IsMouseDown(MouseButton button) {
    const int b = static_cast<int>(button);
    return ValidButton(b) && g.buttons.Held(b);
}

bool Input::WasMousePressed(MouseButton button) {
    const int b = static_cast<int>(button);
    return ValidButton(b) && g.buttons.pressed[b];
}

bool Input::WasMouseReleased(MouseButton button) {
    const int b = static_cast<int>(button);
    return ValidButton(b) && g.buttons.released[b];
}

glm::vec2 Input::MousePosition() {
    return g.mousePos;
}

glm::vec2 Input::MouseDelta() {
    if (g.ignoreDeltaThisFrame) return glm::vec2{0.0f};
    return g.mousePos - g.prevMousePos;
}

float Input::ScrollDelta() {
    return g.scroll;
}

glm::vec2 Input::MouseVelocity() {
    return MouseDelta() / g.frameTime;
}

float Input::FrameTime() {
    return g.frameTime;
}

glm::vec2 Input::MouseVelocityLastFrame() {
    return g.prevVelocity;
}

float Input::ScrollDeltaLastFrame() {
    return g.prevScroll;
}

void Input::SetMousePosition(glm::vec2 position) {
    OnCursorPos(nullptr, position.x, position.y);
}

// --- Controle -------------------------------------------------------------------------------------------

namespace {

// Botão em um controle (`pad` >= 0) ou em qualquer um (kAnyDevice); `prev` lê o quadro anterior.
bool PadButton(int b, int pad, bool prev) {
    if (!ValidPadButton(b)) return false;
    const auto read = [&](const State::Pad& p) { return prev ? p.prevButtons[b] : p.buttons[b]; };
    if (pad >= 0) return ValidPad(pad) && read(g.pads[static_cast<size_t>(pad)]);
    if (pad != kAnyDevice) return false;
    return std::any_of(g.pads.begin(), g.pads.end(), read);
}

// Eixo de um controle, ou o mais empurrado entre todos (dois jogando "juntos" não se somam nem se anulam).
float PadAxis(int a, int pad, bool prev) {
    if (!ValidPadAxis(a)) return 0.0f;
    const auto read = [&](const State::Pad& p) { return prev ? p.prevAxes[a] : p.axes[a]; };
    if (pad >= 0) return ValidPad(pad) ? read(g.pads[static_cast<size_t>(pad)]) : 0.0f;
    if (pad != kAnyDevice) return 0.0f;
    float best = 0.0f;
    for (const State::Pad& p : g.pads) {
        if (std::abs(read(p)) > std::abs(best)) best = read(p);
    }
    return best;
}

} // namespace

void Input::PollGamepads() {
    if (g.padSimulated) return;
    // Quem caiu libera a vaga; quem chegou pega a primeira livre (as outras não mudam de número).
    for (int pad = 0; pad < kMaxGamepads; ++pad) {
        const State::Pad& p = g.pads[static_cast<size_t>(pad)];
        if (p.connected && !glfwJoystickIsGamepad(p.jid)) DisconnectPad(pad);
    }
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        if (!glfwJoystickIsGamepad(jid)) continue;
        const bool known = std::any_of(g.pads.begin(), g.pads.end(),
                                       [&](const State::Pad& p) { return p.connected && p.jid == jid; });
        if (known) continue;
        const auto free = std::find_if(g.pads.begin(), g.pads.end(), [](const State::Pad& p) { return !p.connected; });
        if (free == g.pads.end()) break;  // mais controles que vagas: os de sobra ficam de fora
        const char* name = glfwGetGamepadName(jid);
        ConnectPad(static_cast<int>(free - g.pads.begin()), jid, name ? name : "Controle");
    }
    for (int pad = 0; pad < kMaxGamepads; ++pad) {
        State::Pad& p = g.pads[static_cast<size_t>(pad)];
        GLFWgamepadstate state;
        if (!p.connected || !glfwGetGamepadState(p.jid, &state)) continue;
        for (int b = 0; b < kGamepadButtonCount; ++b) p.buttons[b] = state.buttons[b] == GLFW_PRESS;
        // Analógicos aos pares (a zona morta é radial), com Y para cima positivo; gatilhos de -1..1 para 0..1.
        for (const auto [ax, ay] : {std::pair{GLFW_GAMEPAD_AXIS_LEFT_X, GLFW_GAMEPAD_AXIS_LEFT_Y},
                                    std::pair{GLFW_GAMEPAD_AXIS_RIGHT_X, GLFW_GAMEPAD_AXIS_RIGHT_Y}}) {
            const glm::vec2 stick = ShapeStick({state.axes[ax], -state.axes[ay]}, g.deadZone);
            p.axes[ax] = stick.x;
            p.axes[ay] = stick.y;
        }
        for (const int t : {GLFW_GAMEPAD_AXIS_LEFT_TRIGGER, GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER}) {
            p.axes[t] = std::clamp((state.axes[t] + 1.0f) * 0.5f, 0.0f, 1.0f);
        }
        NoteGamepadUse(pad);
    }
}

void Input::SetGamepadSimulated(bool simulated, int count) {
    count = simulated ? std::clamp(count, 1, kMaxGamepads) : 0;
    const int current = g.padSimulated ? static_cast<int>(std::count_if(g.pads.begin(), g.pads.end(),
                                                                        [](const State::Pad& p) { return p.connected; }))
                                       : -1;
    if (simulated == g.padSimulated && count == current) return;
    if (simulated != g.padSimulated) {
        // Troca de verdade por simulado (ou o contrário): os de antes saem.
        for (int pad = 0; pad < kMaxGamepads; ++pad) DisconnectPad(pad);
        g.padSimulated = simulated;
    }
    for (int pad = 0; pad < kMaxGamepads; ++pad) {
        const bool wanted = pad < count;
        if (wanted && !g.pads[static_cast<size_t>(pad)].connected) {
            ConnectPad(pad, -1, count == 1 ? "Controle simulado" : std::format("Controle simulado {}", pad + 1));
        } else if (!wanted) {
            DisconnectPad(pad);
        }
    }
}

bool Input::IsGamepadSimulated() {
    return g.padSimulated;
}

void Input::SetGamepadButtonState(GamepadButton button, bool down, int pad) {
    const int b = static_cast<int>(button);
    if (!ValidPadButton(b) || !ValidPad(pad) || !g.pads[static_cast<size_t>(pad)].connected) return;
    g.pads[static_cast<size_t>(pad)].buttons[b] = down;
    NoteGamepadUse(pad);
}

void Input::SetGamepadAxisState(GamepadAxis axis, float value, int pad) {
    const int a = static_cast<int>(axis);
    if (!ValidPadAxis(a) || !ValidPad(pad) || !g.pads[static_cast<size_t>(pad)].connected) return;
    g.pads[static_cast<size_t>(pad)].axes[a] =
        std::clamp(value, a >= static_cast<int>(GamepadAxis::LeftTrigger) ? 0.0f : -1.0f, 1.0f);
    NoteGamepadUse(pad);
}

void Input::SetGamepadDeadZone(float deadZone) {
    g.deadZone = std::isfinite(deadZone) ? std::clamp(deadZone, 0.0f, 0.9f) : kGamepadDeadZone;
}

float Input::GamepadDeadZone() {
    return g.deadZone;
}

bool Input::IsGamepadConnected() {
    return GamepadCount() > 0;
}

bool Input::IsGamepadConnected(int pad) {
    return ValidPad(pad) && g.pads[static_cast<size_t>(pad)].connected;
}

int Input::GamepadCount() {
    return static_cast<int>(std::count_if(g.pads.begin(), g.pads.end(), [](const State::Pad& p) { return p.connected; }));
}

bool Input::WasGamepadConnected(int pad) {
    return ValidPad(pad) && g.pads[static_cast<size_t>(pad)].justConnected;
}

bool Input::WasGamepadDisconnected(int pad) {
    return ValidPad(pad) && g.pads[static_cast<size_t>(pad)].justDisconnected;
}

std::string Input::GamepadName() {
    if (IsGamepadConnected(g.lastGamepad)) return g.pads[static_cast<size_t>(g.lastGamepad)].name;
    for (const State::Pad& p : g.pads) {
        if (p.connected) return p.name;
    }
    return {};
}

std::string Input::GamepadName(int pad) {
    return IsGamepadConnected(pad) ? g.pads[static_cast<size_t>(pad)].name : std::string();
}

bool Input::IsGamepadButtonDown(GamepadButton button, int pad) {
    return PadButton(static_cast<int>(button), pad, false);
}

bool Input::WasGamepadButtonPressed(GamepadButton button, int pad) {
    const int b = static_cast<int>(button);
    return PadButton(b, pad, false) && !PadButton(b, pad, true);
}

bool Input::WasGamepadButtonReleased(GamepadButton button, int pad) {
    const int b = static_cast<int>(button);
    return !PadButton(b, pad, false) && PadButton(b, pad, true);
}

float Input::GamepadAxisValue(GamepadAxis axis, int pad) {
    return PadAxis(static_cast<int>(axis), pad, false);
}

InputDevice Input::LastDevice() {
    return g.lastDevice;
}

int Input::LastGamepad() {
    return g.lastGamepad;
}

bool Input::WasKeyDownLastFrame(Key key) {
    const int k = static_cast<int>(key);
    return ValidKey(k) && g.keys.prevHeld[k];
}

bool Input::WasMouseDownLastFrame(MouseButton button) {
    const int b = static_cast<int>(button);
    return ValidButton(b) && g.buttons.prevHeld[b];
}

bool Input::WasGamepadButtonDownLastFrame(GamepadButton button, int pad) {
    return PadButton(static_cast<int>(button), pad, true);
}

float Input::GamepadAxisValueLastFrame(GamepadAxis axis, int pad) {
    return PadAxis(static_cast<int>(axis), pad, true);
}

// --- Nomes ----------------------------------------------------------------------------------------------

const char* KeyName(Key key) {
    const int k = static_cast<int>(key);
    return ValidKey(k) ? KeyNames()[k].c_str() : "";
}

std::optional<Key> ParseKeyName(std::string_view name) {
    const std::string lower = Lower(std::string(name));
    if (lower.empty()) return std::nullopt;
    const auto& names = KeyNames();
    for (int k = 0; k < kMaxKeys; ++k) {
        if (names[k].size() == lower.size() && Lower(names[k]) == lower) return static_cast<Key>(k);
    }
    return std::nullopt;
}

const char* GamepadButtonName(GamepadButton button) {
    const int b = static_cast<int>(button);
    return ValidPadButton(b) ? kPadButtonNames[b] : "";
}

std::optional<GamepadButton> ParseGamepadButton(std::string_view name) {
    const std::string lower = Lower(std::string(name));
    for (int b = 0; b < kGamepadButtonCount; ++b) {
        if (Lower(kPadButtonNames[b]) == lower) return static_cast<GamepadButton>(b);
    }
    return std::nullopt;
}

const char* GamepadAxisName(GamepadAxis axis) {
    const int a = static_cast<int>(axis);
    return ValidPadAxis(a) ? kPadAxisNames[a] : "";
}

std::optional<GamepadAxis> ParseGamepadAxis(std::string_view name) {
    const std::string lower = Lower(std::string(name));
    for (int a = 0; a < kGamepadAxisCount; ++a) {
        if (Lower(kPadAxisNames[a]) == lower) return static_cast<GamepadAxis>(a);
    }
    return std::nullopt;
}

const char* MouseButtonName(MouseButton button) {
    const int b = static_cast<int>(button);
    return b >= 0 && b < kMouseButtonCount ? kMouseNames[b] : "";
}

std::optional<MouseButton> ParseMouseButton(std::string_view name) {
    const std::string lower = Lower(std::string(name));
    for (int b = 0; b < kMouseButtonCount; ++b) {
        if (Lower(kMouseNames[b]) == lower) return static_cast<MouseButton>(b);
    }
    return std::nullopt;
}

} // namespace motor
