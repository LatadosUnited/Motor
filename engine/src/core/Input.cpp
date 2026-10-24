#include "motor/core/Input.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
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
    bool hasMousePos = false;
    bool ignoreDeltaRequested = false;
    bool ignoreDeltaThisFrame = false;
    std::string text;
    // Controle
    std::array<bool, kGamepadButtonCount> pad{};
    std::array<bool, kGamepadButtonCount> prevPad{};
    std::array<float, kGamepadAxisCount> axes{};
    std::array<float, kGamepadAxisCount> prevAxes{};
    bool padConnected = false;
    bool padSimulated = false;
    std::string padName;
    float deadZone = Input::kGamepadDeadZone;
} g;

bool ValidPadButton(int b) { return b >= 0 && b < kGamepadButtonCount; }
bool ValidPadAxis(int a) { return a >= 0 && a < kGamepadAxisCount; }

std::string Lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return out;
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
    if (!ValidKey(key) || action == GLFW_REPEAT) return;
    g.keys.Set(key, action == GLFW_PRESS);
}

void OnChar(GLFWwindow*, unsigned int codepoint) {
    AppendUtf8(g.text, codepoint);
}

void OnMouseButton(GLFWwindow*, int button, int action, int) {
    if (!ValidButton(button)) return;
    g.buttons.Set(button, action == GLFW_PRESS);
}

void OnCursorPos(GLFWwindow*, double x, double y) {
    g.mousePos = {static_cast<float>(x), static_cast<float>(y)};
    if (!g.hasMousePos) {
        g.prevMousePos = g.mousePos;
        g.hasMousePos = true;
    }
}

void OnScroll(GLFWwindow*, double, double yOffset) {
    g.scroll += static_cast<float>(yOffset);
}

void OnFocus(GLFWwindow*, int focused) {
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

void Input::Attach(GLFWwindow* window) {
    glfwSetKeyCallback(window, OnKey);
    glfwSetCharCallback(window, OnChar);
    glfwSetMouseButtonCallback(window, OnMouseButton);
    glfwSetCursorPosCallback(window, OnCursorPos);
    glfwSetScrollCallback(window, OnScroll);
    glfwSetWindowFocusCallback(window, OnFocus);
}

void Input::BeginFrame() {
    g.keys.BeginFrame();
    g.buttons.BeginFrame();
    g.prevPad = g.pad;
    g.prevAxes = g.axes;
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
    if (ValidKey(k)) g.keys.Set(k, down);
}

void Input::SetMouseButtonState(MouseButton button, bool down) {
    const int b = static_cast<int>(button);
    if (ValidButton(b)) g.buttons.Set(b, down);
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

// --- Controle -------------------------------------------------------------------------------------------

void Input::PollGamepads() {
    if (g.padSimulated) return;
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        GLFWgamepadstate state;
        if (!glfwJoystickIsGamepad(jid) || !glfwGetGamepadState(jid, &state)) continue;
        if (!g.padConnected) {
            const char* name = glfwGetGamepadName(jid);
            g.padName = name ? name : "Controle";
        }
        g.padConnected = true;
        for (int b = 0; b < kGamepadButtonCount; ++b) g.pad[b] = state.buttons[b] == GLFW_PRESS;
        // Analógicos aos pares (a zona morta é radial), com Y para cima positivo; gatilhos de -1..1 para 0..1.
        for (const auto [ax, ay] : {std::pair{GLFW_GAMEPAD_AXIS_LEFT_X, GLFW_GAMEPAD_AXIS_LEFT_Y},
                                    std::pair{GLFW_GAMEPAD_AXIS_RIGHT_X, GLFW_GAMEPAD_AXIS_RIGHT_Y}}) {
            const glm::vec2 stick = ShapeStick({state.axes[ax], -state.axes[ay]}, g.deadZone);
            g.axes[ax] = stick.x;
            g.axes[ay] = stick.y;
        }
        for (const int t : {GLFW_GAMEPAD_AXIS_LEFT_TRIGGER, GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER}) {
            g.axes[t] = std::clamp((state.axes[t] + 1.0f) * 0.5f, 0.0f, 1.0f);
        }
        return;
    }
    g.padConnected = false;
    g.padName.clear();
    g.pad.fill(false);
    g.axes.fill(0.0f);
}

void Input::SetGamepadSimulated(bool simulated) {
    if (g.padSimulated == simulated) return;
    g.padSimulated = simulated;
    g.padConnected = simulated;
    g.padName = simulated ? "Controle simulado" : "";
    g.pad.fill(false);
    g.axes.fill(0.0f);
}

bool Input::IsGamepadSimulated() {
    return g.padSimulated;
}

void Input::SetGamepadButtonState(GamepadButton button, bool down) {
    const int b = static_cast<int>(button);
    if (ValidPadButton(b)) g.pad[b] = down;
}

void Input::SetGamepadAxisState(GamepadAxis axis, float value) {
    const int a = static_cast<int>(axis);
    if (ValidPadAxis(a)) g.axes[a] = std::clamp(value, a >= static_cast<int>(GamepadAxis::LeftTrigger) ? 0.0f : -1.0f, 1.0f);
}

void Input::SetGamepadDeadZone(float deadZone) {
    g.deadZone = std::isfinite(deadZone) ? std::clamp(deadZone, 0.0f, 0.9f) : kGamepadDeadZone;
}

float Input::GamepadDeadZone() {
    return g.deadZone;
}

bool Input::IsGamepadConnected() {
    return g.padConnected;
}

std::string Input::GamepadName() {
    return g.padName;
}

bool Input::IsGamepadButtonDown(GamepadButton button) {
    const int b = static_cast<int>(button);
    return ValidPadButton(b) && g.pad[b];
}

bool Input::WasGamepadButtonPressed(GamepadButton button) {
    const int b = static_cast<int>(button);
    return ValidPadButton(b) && g.pad[b] && !g.prevPad[b];
}

bool Input::WasGamepadButtonReleased(GamepadButton button) {
    const int b = static_cast<int>(button);
    return ValidPadButton(b) && !g.pad[b] && g.prevPad[b];
}

float Input::GamepadAxisValue(GamepadAxis axis) {
    const int a = static_cast<int>(axis);
    return ValidPadAxis(a) ? g.axes[a] : 0.0f;
}

bool Input::WasKeyDownLastFrame(Key key) {
    const int k = static_cast<int>(key);
    return ValidKey(k) && g.keys.prevHeld[k];
}

bool Input::WasMouseDownLastFrame(MouseButton button) {
    const int b = static_cast<int>(button);
    return ValidButton(b) && g.buttons.prevHeld[b];
}

bool Input::WasGamepadButtonDownLastFrame(GamepadButton button) {
    const int b = static_cast<int>(button);
    return ValidPadButton(b) && g.prevPad[b];
}

float Input::GamepadAxisValueLastFrame(GamepadAxis axis) {
    const int a = static_cast<int>(axis);
    return ValidPadAxis(a) ? g.prevAxes[a] : 0.0f;
}

// --- Nomes ----------------------------------------------------------------------------------------------

const char* KeyName(Key key) {
    const int k = static_cast<int>(key);
    return ValidKey(k) ? KeyNames()[k].c_str() : "";
}

std::optional<Key> ParseKeyName(std::string_view name) {
    const std::string lower = Lower(name);
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
    const std::string lower = Lower(name);
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
    const std::string lower = Lower(name);
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
    const std::string lower = Lower(name);
    for (int b = 0; b < kMouseButtonCount; ++b) {
        if (Lower(kMouseNames[b]) == lower) return static_cast<MouseButton>(b);
    }
    return std::nullopt;
}

} // namespace motor
