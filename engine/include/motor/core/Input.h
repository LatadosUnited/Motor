#pragma once

#include <glm/vec2.hpp>

#include <cstdint>
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

// Vários controles: cada um ganha uma vaga (0 a kMaxGamepads - 1) ao ligar e a mantém até desligar, então o
// controle 1 continua sendo o 1 quando o 0 cai. Nas funções e ações que recebem `device`: kAnyDevice é tudo junto
// (jogo de uma pessoa: qualquer controle serve), kKeyboardMouse só o teclado e o mouse, e 0..3 só aquele controle.
inline constexpr int kMaxGamepads = 4;
inline constexpr int kAnyDevice = -1;
inline constexpr int kKeyboardMouse = -2;

// O último usado: para a tela mostrar "Espaço" ou "A". Muda ao apertar uma tecla ou botão, mover o mouse ou
// empurrar um analógico mais da metade.
enum class InputDevice { KeyboardMouse, Gamepad };

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
    // Velocidade do mouse no quadro (pixels por segundo, Y da tela para baixo): MouseDelta / tempo do quadro. É o
    // que as ações usam para pôr o mouse e o analógico na mesma medida.
    static glm::vec2 MouseVelocity();
    static float FrameTime();  // segundos desde o BeginFrame anterior

    // Texto digitado neste quadro, em UTF-8, já com o layout e os acentos do sistema ("ç", "ã"). Para campos de
    // texto; para comandos, use as teclas.
    static const std::string& TextInput();

    // Controles que a GLFW reconhece como gamepad. Sem `pad`: todos juntos (botão apertado em qualquer um; eixo do
    // que estiver mais empurrado). Com `pad` (0 a kMaxGamepads - 1): só aquele. Sem controle, tudo solto/zero.
    static bool IsGamepadConnected();
    static bool IsGamepadConnected(int pad);
    static int GamepadCount();
    static bool WasGamepadConnected(int pad);     // ligou neste frame
    static bool WasGamepadDisconnected(int pad);  // desligou neste frame
    static std::string GamepadName();  // o do último usado (ou o primeiro ligado)
    static std::string GamepadName(int pad);
    static bool IsGamepadButtonDown(GamepadButton button, int pad = kAnyDevice);
    static bool WasGamepadButtonPressed(GamepadButton button, int pad = kAnyDevice);
    static bool WasGamepadButtonReleased(GamepadButton button, int pad = kAnyDevice);
    // Com zona morta nos analógicos (radial, ver ShapeStick): o controle parado dá 0, e o valor vai de 0 a 1
    // depois dela. Os gatilhos não têm zona morta.
    static float GamepadAxisValue(GamepadAxis axis, int pad = kAnyDevice);

    static InputDevice LastDevice();
    static int LastGamepad();  // vaga do último controle usado; -1 se nenhum foi usado
    static constexpr float kGamepadDeadZone = 0.2f;  // o padrão
    static void SetGamepadDeadZone(float deadZone);  // limitada a 0..0.9; vale a partir da próxima leitura
    static float GamepadDeadZone();

    // Estado do frame anterior (mapa de ações: "apertou neste frame").
    static bool WasKeyDownLastFrame(Key key);
    static bool WasMouseDownLastFrame(MouseButton button);
    static bool WasGamepadButtonDownLastFrame(GamepadButton button, int pad = kAnyDevice);
    static float GamepadAxisValueLastFrame(GamepadAxis axis, int pad = kAnyDevice);
    static glm::vec2 MouseVelocityLastFrame();
    static float ScrollDeltaLastFrame();

    // --- Uso interno da engine ---
    // Eventos de teclado, mouse e foco recebidos desde o início: mudou entre dois quadros, o usuário mexeu em algo
    // (a Application usa para dormir quando ninguém mexe, ver SetIdleWait).
    static uint64_t EventCount();
    static bool IsAnyHeld();  // alguma tecla ou botão do mouse apertado (segurar sem mexer também é uso)
    static void Attach(GLFWwindow* window);
    // Chamar antes de processar os eventos do frame. `frameSeconds` > 0 fixa o tempo do quadro (testes); sem ele,
    // é medido no relógio.
    static void BeginFrame(float frameSeconds = 0.0f);
    static void SetMousePosition(glm::vec2 position);  // testes e MCP: como o evento da janela
    static void IgnoreNextMouseDelta();
    // Testes e MCP: simulam no estado interno, não no sistema, como os eventos da janela (contam no quadro).
    static void SetKeyState(Key key, bool down);
    static void SetMouseButtonState(MouseButton button, bool down);
    static void AddTextInput(std::string_view utf8);
    static void AddScroll(float notches);  // como o evento da roda (positivo = para cima)
    static void PollGamepads();  // depois dos eventos do frame (Window::PollEvents)
    // Controles simulados (testes, MCP): enquanto ligado, os de verdade são ignorados e vale o estado posto aqui.
    // Ligar simula `count` controles nas vagas 0 em diante (cada um avisa WasGamepadConnected); desligar volta aos
    // de verdade.
    static void SetGamepadSimulated(bool simulated, int count = 1);
    static bool IsGamepadSimulated();
    static void SetGamepadButtonState(GamepadButton button, bool down, int pad = 0);
    static void SetGamepadAxisState(GamepadAxis axis, float value, int pad = 0);  // já no sentido da engine (Y para cima +)
};

} // namespace motor
