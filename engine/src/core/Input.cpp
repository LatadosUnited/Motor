#include "motor/core/Input.h"

#include <GLFW/glfw3.h>

#include <array>

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
} g;

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

} // namespace motor
