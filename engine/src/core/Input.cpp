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

struct State {
    std::array<bool, kMaxKeys> keys{};
    std::array<bool, kMaxKeys> prevKeys{};
    std::array<bool, kMaxButtons> buttons{};
    std::array<bool, kMaxButtons> prevButtons{};
    glm::vec2 mousePos{0.0f};
    glm::vec2 prevMousePos{0.0f};
    float scroll = 0.0f;
    bool hasMousePos = false;
    bool ignoreDeltaRequested = false;
    bool ignoreDeltaThisFrame = false;
    // Controle
    std::array<bool, kGamepadButtonCount> pad{};
    std::array<bool, kGamepadButtonCount> prevPad{};
    std::array<float, kGamepadAxisCount> axes{};
    std::array<float, kGamepadAxisCount> prevAxes{};
    bool padConnected = false;
    bool padSimulated = false;
    std::string padName;
} g;

bool ValidPadButton(int b) { return b >= 0 && b < kGamepadButtonCount; }
bool ValidPadAxis(int a) { return a >= 0 && a < kGamepadAxisCount; }

// Analógico: zona morta e o resto reescalado para 0..1; gatilho: de -1..1 (GLFW) para 0..1.
float ShapeAxis(int axis, float raw) {
    if (axis >= static_cast<int>(GamepadAxis::LeftTrigger)) return std::clamp((raw + 1.0f) * 0.5f, 0.0f, 1.0f);
    const float magnitude = std::abs(raw);
    if (magnitude <= Input::kGamepadDeadZone) return 0.0f;
    const float scaled = std::min((magnitude - Input::kGamepadDeadZone) / (1.0f - Input::kGamepadDeadZone), 1.0f);
    return raw < 0.0f ? -scaled : scaled;
}

std::string Lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return out;
}

constexpr const char* kPadButtonNames[kGamepadButtonCount] = {
    "A", "B", "X", "Y", "LeftBumper", "RightBumper", "Back", "Start", "Guide", "LeftThumb", "RightThumb",
    "DpadUp", "DpadRight", "DpadDown", "DpadLeft"};
constexpr const char* kPadAxisNames[kGamepadAxisCount] = {"LeftX", "LeftY", "RightX", "RightY", "LeftTrigger",
                                                         "RightTrigger"};
constexpr const char* kMouseNames[] = {"MouseLeft", "MouseRight", "MouseMiddle"};

struct NamedKey {
    Key key;
    const char* name;
};
constexpr NamedKey kNamedKeys[] = {
    {Key::Space, "Space"}, {Key::Apostrophe, "Apostrophe"}, {Key::Comma, "Comma"}, {Key::Minus, "Minus"},
    {Key::Period, "Period"}, {Key::Slash, "Slash"}, {Key::Semicolon, "Semicolon"}, {Key::Equal, "Equal"},
    {Key::Escape, "Escape"}, {Key::Enter, "Enter"}, {Key::Tab, "Tab"}, {Key::Backspace, "Backspace"},
    {Key::Insert, "Insert"}, {Key::Delete, "Delete"}, {Key::Right, "Right"}, {Key::Left, "Left"},
    {Key::Down, "Down"}, {Key::Up, "Up"}, {Key::PageUp, "PageUp"}, {Key::PageDown, "PageDown"},
    {Key::Home, "Home"}, {Key::End, "End"}, {Key::LeftShift, "LeftShift"}, {Key::LeftControl, "LeftControl"},
    {Key::LeftAlt, "LeftAlt"}, {Key::LeftSuper, "LeftSuper"}, {Key::RightShift, "RightShift"},
    {Key::RightControl, "RightControl"}, {Key::RightAlt, "RightAlt"}, {Key::RightSuper, "RightSuper"}};


bool ValidKey(int k) { return k >= 0 && k < kMaxKeys; }
bool ValidButton(int b) { return b >= 0 && b < kMaxButtons; }

void OnKey(GLFWwindow*, int key, int, int action, int) {
    if (!ValidKey(key) || action == GLFW_REPEAT) return;
    g.keys[key] = action == GLFW_PRESS;
}

void OnMouseButton(GLFWwindow*, int button, int action, int) {
    if (!ValidButton(button)) return;
    g.buttons[button] = action == GLFW_PRESS;
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
    // Evita teclas "presas" quando a janela perde o foco.
    if (!focused) {
        g.keys.fill(false);
        g.buttons.fill(false);
    }
}
} // namespace

void Input::Attach(GLFWwindow* window) {
    glfwSetKeyCallback(window, OnKey);
    glfwSetMouseButtonCallback(window, OnMouseButton);
    glfwSetCursorPosCallback(window, OnCursorPos);
    glfwSetScrollCallback(window, OnScroll);
    glfwSetWindowFocusCallback(window, OnFocus);
}

void Input::BeginFrame() {
    g.prevKeys = g.keys;
    g.prevButtons = g.buttons;
    g.prevPad = g.pad;
    g.prevAxes = g.axes;
    g.prevMousePos = g.mousePos;
    g.scroll = 0.0f;
    g.ignoreDeltaThisFrame = g.ignoreDeltaRequested;
    g.ignoreDeltaRequested = false;
}

void Input::IgnoreNextMouseDelta() {
    g.ignoreDeltaRequested = true;
}

void Input::SetKeyState(Key key, bool down) {
    const int k = static_cast<int>(key);
    if (ValidKey(k)) g.keys[k] = down;
}

bool Input::IsKeyDown(Key key) {
    const int k = static_cast<int>(key);
    return ValidKey(k) && g.keys[k];
}

bool Input::WasKeyPressed(Key key) {
    const int k = static_cast<int>(key);
    return ValidKey(k) && g.keys[k] && !g.prevKeys[k];
}

bool Input::WasKeyReleased(Key key) {
    const int k = static_cast<int>(key);
    return ValidKey(k) && !g.keys[k] && g.prevKeys[k];
}

bool Input::IsMouseDown(MouseButton button) {
    const int b = static_cast<int>(button);
    return ValidButton(b) && g.buttons[b];
}

bool Input::WasMousePressed(MouseButton button) {
    const int b = static_cast<int>(button);
    return ValidButton(b) && g.buttons[b] && !g.prevButtons[b];
}

bool Input::WasMouseReleased(MouseButton button) {
    const int b = static_cast<int>(button);
    return ValidButton(b) && !g.buttons[b] && g.prevButtons[b];
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
        for (int a = 0; a < kGamepadAxisCount; ++a) {
            float raw = state.axes[a];
            if (a == static_cast<int>(GamepadAxis::LeftY) || a == static_cast<int>(GamepadAxis::RightY)) raw = -raw;
            g.axes[a] = ShapeAxis(a, raw);
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
    return ValidKey(k) && g.prevKeys[k];
}

bool Input::WasMouseDownLastFrame(MouseButton button) {
    const int b = static_cast<int>(button);
    return ValidButton(b) && g.prevButtons[b];
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
    static char letters[26][2];
    static char digits[10][2];
    static char functions[12][4];
    const int k = static_cast<int>(key);
    if (k >= static_cast<int>(Key::A) && k <= static_cast<int>(Key::Z)) {
        char* t = letters[k - static_cast<int>(Key::A)];
        t[0] = static_cast<char>('A' + (k - static_cast<int>(Key::A)));
        return t;
    }
    if (k >= static_cast<int>(Key::D0) && k <= static_cast<int>(Key::D9)) {
        char* t = digits[k - static_cast<int>(Key::D0)];
        t[0] = static_cast<char>('0' + (k - static_cast<int>(Key::D0)));
        return t;
    }
    if (k >= static_cast<int>(Key::F1) && k <= static_cast<int>(Key::F12)) {
        char* t = functions[k - static_cast<int>(Key::F1)];
        const int n = k - static_cast<int>(Key::F1) + 1;
        t[0] = 'F';
        t[1] = static_cast<char>(n >= 10 ? '1' : '0' + n);
        t[2] = n >= 10 ? static_cast<char>('0' + n - 10) : '\0';
        return t;
    }
    for (const NamedKey& named : kNamedKeys) {
        if (named.key == key) return named.name;
    }
    return "";
}

std::optional<Key> ParseKeyName(std::string_view name) {
    const std::string lower = Lower(name);
    if (lower.size() == 1 && lower[0] >= 'a' && lower[0] <= 'z') return Key(int(Key::A) + (lower[0] - 'a'));
    if (lower.size() == 1 && lower[0] >= '0' && lower[0] <= '9') return Key(int(Key::D0) + (lower[0] - '0'));
    if (lower.size() >= 2 && lower.size() <= 3 && lower[0] == 'f' && std::isdigit(static_cast<unsigned char>(lower[1]))) {
        const int n = std::atoi(lower.c_str() + 1);
        if (n >= 1 && n <= 12) return Key(int(Key::F1) + n - 1);
    }
    for (const NamedKey& named : kNamedKeys) {
        if (Lower(named.name) == lower) return named.key;
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
    return b >= 0 && b < 3 ? kMouseNames[b] : "";
}

std::optional<MouseButton> ParseMouseButton(std::string_view name) {
    const std::string lower = Lower(name);
    for (int b = 0; b < 3; ++b) {
        if (Lower(kMouseNames[b]) == lower) return static_cast<MouseButton>(b);
    }
    return std::nullopt;
}

} // namespace motor
