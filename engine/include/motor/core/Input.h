#pragma once

#include <glm/vec2.hpp>

struct GLFWwindow;

namespace motor {

// Valores idênticos aos códigos do GLFW, para conversão direta.
enum class Key : int {
    Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
    D0 = 48, D1, D2, D3, D4, D5, D6, D7, D8, D9,
    Semicolon = 59, Equal = 61,
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Escape = 256, Enter, Tab, Backspace, Insert, Delete, Right, Left, Down, Up, PageUp, PageDown, Home, End,
    F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    LeftShift = 340, LeftControl, LeftAlt, LeftSuper, RightShift, RightControl, RightAlt, RightSuper,
};

enum class MouseButton : int { Left = 0, Right = 1, Middle = 2 };

class Input {
public:
    static bool IsKeyDown(Key key);
    static bool WasKeyPressed(Key key);   // desceu neste frame
    static bool WasKeyReleased(Key key);  // subiu neste frame

    static bool IsMouseDown(MouseButton button);
    static bool WasMousePressed(MouseButton button);
    static bool WasMouseReleased(MouseButton button);

    static glm::vec2 MousePosition();
    static glm::vec2 MouseDelta();
    static float ScrollDelta();

    // --- Uso interno da engine ---
    static void Attach(GLFWwindow* window);
    static void BeginFrame();  // chamar antes de processar os eventos do frame
    static void IgnoreNextMouseDelta();
};

} // namespace motor
