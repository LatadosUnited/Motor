#pragma once

#include <glm/vec2.hpp>

#include <optional>
#include <string>
#include <string_view>

struct GLFWwindow;

namespace motor {

// Valores idênticos aos códigos do GLFW, para conversão direta. São teclas físicas na posição do teclado
// americano (no ABNT2, Semicolon é o Ç). World1 e World2 são as teclas a mais de layouts fora do americano.
enum class Key : int {
    Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
    D0 = 48, D1, D2, D3, D4, D5, D6, D7, D8, D9,
    Semicolon = 59, Equal = 61,
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftBracket = 91, Backslash, RightBracket, GraveAccent = 96, World1 = 161, World2,
    Escape = 256, Enter, Tab, Backspace, Insert, Delete, Right, Left, Down, Up, PageUp, PageDown, Home, End,
    CapsLock = 280, ScrollLock, NumLock, PrintScreen, Pause,
    F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22,
    F23, F24,
    Numpad0 = 320, Numpad1, Numpad2, Numpad3, Numpad4, Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
    NumpadDecimal, NumpadDivide, NumpadMultiply, NumpadSubtract, NumpadAdd, NumpadEnter, NumpadEqual,
    LeftShift = 340, LeftControl, LeftAlt, LeftSuper, RightShift, RightControl, RightAlt, RightSuper, Menu,
};
inline constexpr Key kFirstKey = Key::Space;  // para percorrer todas: de kFirstKey a kLastKey, pulando as sem nome
inline constexpr Key kLastKey = Key::Menu;

// Back e Forward são os botões laterais (4 e 5) dos mouses que os têm.
enum class MouseButton : int { Left = 0, Right = 1, Middle = 2, Back = 3, Forward = 4 };
inline constexpr int kMouseButtonCount = 5;

// Controle (gamepad) no padrão do Xbox: a GLFW traduz Xbox, PlayStation, Switch e outros pelo banco de mapeamentos
// do SDL (o "A" é o botão de baixo, o "X" do PlayStation). Valores idênticos aos do GLFW.
enum class GamepadButton : int {
    A = 0, B, X, Y, LeftBumper, RightBumper, Back, Start, Guide, LeftThumb, RightThumb,
    DpadUp, DpadRight, DpadDown, DpadLeft,
};
inline constexpr int kGamepadButtonCount = 15;
// Analógicos de -1 a 1 (Y para cima é positivo) e gatilhos de 0 a 1.
enum class GamepadAxis : int { LeftX = 0, LeftY, RightX, RightY, LeftTrigger, RightTrigger };
inline constexpr int kGamepadAxisCount = 6;

// Nomes para arquivos, menus e o MCP ("Space", "A", "LeftX"...). Parse ignora maiúsculas.
const char* KeyName(Key key);  // "" se a tecla não tem nome
std::optional<Key> ParseKeyName(std::string_view name);
const char* GamepadButtonName(GamepadButton button);
std::optional<GamepadButton> ParseGamepadButton(std::string_view name);
const char* GamepadAxisName(GamepadAxis axis);
std::optional<GamepadAxis> ParseGamepadAxis(std::string_view name);
const char* MouseButtonName(MouseButton button);
std::optional<MouseButton> ParseMouseButton(std::string_view name);

// Cursor do mouse: Normal; Hidden (escondido sobre a janela, mas anda e sai dela); Locked (escondido e preso no
// centro: o mouse só gera MouseDelta, para olhar em volta).
enum class CursorMode { Normal, Hidden, Locked };

// Analógico com zona morta radial: abaixo de `deadZone` (comprimento do vetor) dá zero, e o resto é reescalado
// para 0..1 mantendo a direção. Por eixo separado, a diagonal "grudava" nos eixos.
glm::vec2 ShapeStick(glm::vec2 raw, float deadZone);

// Teclas e botões contam os eventos do quadro, não só o estado no fim dele: um toque mais curto que um quadro
// (desceu e subiu entre dois quadros) dá WasKeyPressed e WasKeyReleased juntos, e IsKeyDown vale nesse quadro.
class Input {
public:
    static bool IsKeyDown(Key key);       // apertada agora, ou apertada em algum momento deste quadro
    static bool WasKeyPressed(Key key);   // desceu neste frame
    static bool WasKeyReleased(Key key);  // subiu neste frame

    static bool IsMouseDown(MouseButton button);
    static bool WasMousePressed(MouseButton button);
    static bool WasMouseReleased(MouseButton button);

    static glm::vec2 MousePosition();
    static glm::vec2 MouseDelta();
    static float ScrollDelta();

    // Texto digitado neste quadro, em UTF-8, já com o layout e os acentos do sistema ("ç", "ã"). Para campos de
    // texto; para comandos, use as teclas.
    static const std::string& TextInput();

    // Controle: o primeiro conectado que a GLFW reconhece como gamepad. Sem controle, tudo solto/zero.
    static bool IsGamepadConnected();
    static std::string GamepadName();
    static bool IsGamepadButtonDown(GamepadButton button);
    static bool WasGamepadButtonPressed(GamepadButton button);
    static bool WasGamepadButtonReleased(GamepadButton button);
    // Com zona morta nos analógicos (radial, ver ShapeStick): o controle parado dá 0, e o valor vai de 0 a 1
    // depois dela. Os gatilhos não têm zona morta.
    static float GamepadAxisValue(GamepadAxis axis);
    static constexpr float kGamepadDeadZone = 0.2f;  // o padrão
    static void SetGamepadDeadZone(float deadZone);  // limitada a 0..0.9; vale a partir da próxima leitura
    static float GamepadDeadZone();

    // Estado do frame anterior (mapa de ações: "apertou neste frame").
    static bool WasKeyDownLastFrame(Key key);
    static bool WasMouseDownLastFrame(MouseButton button);
    static bool WasGamepadButtonDownLastFrame(GamepadButton button);
    static float GamepadAxisValueLastFrame(GamepadAxis axis);

    // --- Uso interno da engine ---
    static void Attach(GLFWwindow* window);
    static void BeginFrame();  // chamar antes de processar os eventos do frame
    static void IgnoreNextMouseDelta();
    // Testes e MCP: simulam no estado interno, não no sistema, como os eventos da janela (contam no quadro).
    static void SetKeyState(Key key, bool down);
    static void SetMouseButtonState(MouseButton button, bool down);
    static void AddTextInput(std::string_view utf8);
    static void PollGamepads();  // depois dos eventos do frame (Window::PollEvents)
    // Controle simulado (testes, MCP): enquanto ligado, o controle de verdade é ignorado e vale o estado posto aqui.
    static void SetGamepadSimulated(bool simulated);
    static bool IsGamepadSimulated();
    static void SetGamepadButtonState(GamepadButton button, bool down);
    static void SetGamepadAxisState(GamepadAxis axis, float value);  // já no sentido da engine (Y para cima +)
};

} // namespace motor
