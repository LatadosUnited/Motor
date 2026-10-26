// Entrada bruta sem janela: eventos do quadro (toque mais curto que um quadro não se perde, em teclas, botões do
// mouse e ações), teclas e botões novos com nome, texto digitado, a zona morta radial dos analógicos e vários
// controles (vagas, ligar e desligar, ações por jogador, último dispositivo usado).
#include "TestUtil.h"

#include <motor/core/Input.h>
#include <motor/project/InputActions.h>

#include <cmath>
#include <limits>
#include <string>

using namespace motor;

namespace {

bool Near(float a, float b) { return std::abs(a - b) < 1e-4f; }
bool Near(glm::vec2 a, glm::vec2 b) { return Near(a.x, b.x) && Near(a.y, b.y); }
float Length(glm::vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

} // namespace

int main() {
    InputActions actions;
    actions.actions = {{"Pular", {InputBinding::Of(Key::J)}}, {"Atirar", {InputBinding::Of(MouseButton::Back)}}};

    SECTION("Toque dentro de um quadro");
    {
        Input::BeginFrame();
        Input::SetKeyState(Key::J, true);
        Input::SetKeyState(Key::J, false);  // desceu e subiu antes do quadro seguinte
        CHECK(Input::WasKeyPressed(Key::J) && Input::WasKeyReleased(Key::J));
        CHECK(Input::IsKeyDown(Key::J));  // vale neste quadro, para quem só olha o estado
        CHECK(actions.IsDown("Pular") && actions.WasPressed("Pular"));
        Input::BeginFrame();
        CHECK(!Input::IsKeyDown(Key::J) && !Input::WasKeyPressed(Key::J) && !Input::WasKeyReleased(Key::J));
        CHECK(Input::WasKeyDownLastFrame(Key::J));
        CHECK(!actions.IsDown("Pular") && actions.WasReleased("Pular"));  // a ação solta no quadro seguinte
        Input::BeginFrame();
        CHECK(!actions.WasReleased("Pular") && !Input::WasKeyDownLastFrame(Key::J));
    }

    SECTION("Tecla segurada e apertada de novo");
    {
        Input::BeginFrame();
        Input::SetKeyState(Key::J, true);
        Input::SetKeyState(Key::J, true);  // repetir o mesmo estado não é um evento novo
        CHECK(Input::WasKeyPressed(Key::J) && !Input::WasKeyReleased(Key::J));
        Input::BeginFrame();
        CHECK(Input::IsKeyDown(Key::J) && !Input::WasKeyPressed(Key::J) && !actions.WasPressed("Pular"));
        Input::SetKeyState(Key::J, false);
        Input::SetKeyState(Key::J, true);  // soltou e apertou no mesmo quadro
        CHECK(Input::WasKeyReleased(Key::J) && Input::WasKeyPressed(Key::J) && Input::IsKeyDown(Key::J));
        Input::BeginFrame();
        Input::SetKeyState(Key::J, false);
        CHECK(!Input::IsKeyDown(Key::J) && Input::WasKeyReleased(Key::J) && actions.WasReleased("Pular"));
        Input::BeginFrame();
    }

    SECTION("Botões do mouse, incluindo os laterais");
    {
        Input::SetMouseButtonState(MouseButton::Back, true);
        Input::SetMouseButtonState(MouseButton::Back, false);
        CHECK(Input::WasMousePressed(MouseButton::Back) && Input::WasMouseReleased(MouseButton::Back));
        CHECK(Input::IsMouseDown(MouseButton::Back) && actions.WasPressed("Atirar"));
        Input::BeginFrame();
        CHECK(!Input::IsMouseDown(MouseButton::Back) && actions.WasReleased("Atirar"));
        Input::SetMouseButtonState(MouseButton::Forward, true);
        CHECK(Input::IsMouseDown(MouseButton::Forward) && !Input::IsMouseDown(MouseButton::Back));
        Input::SetMouseButtonState(MouseButton::Forward, false);
        Input::BeginFrame();
    }

    SECTION("Nomes das teclas e botões novos");
    {
        for (const auto& [key, name] : {std::pair{Key::F13, "F13"}, {Key::F24, "F24"}, {Key::Numpad0, "Numpad0"},
                                        {Key::Numpad9, "Numpad9"}, {Key::NumpadEnter, "NumpadEnter"},
                                        {Key::LeftBracket, "LeftBracket"}, {Key::GraveAccent, "GraveAccent"},
                                        {Key::World1, "World1"}, {Key::CapsLock, "CapsLock"}, {Key::Menu, "Menu"}}) {
            CHECK(std::string(KeyName(key)) == name);
            CHECK(ParseKeyName(name) == key);
        }
        CHECK(ParseKeyName("numpadadd") == Key::NumpadAdd && ParseKeyName("f9") == Key::F9);
        CHECK(!ParseKeyName("F25") && !ParseKeyName("") && !ParseKeyName("Numpad"));
        CHECK(*KeyName(kFirstKey) && *KeyName(kLastKey));
        for (int b = 0; b < kMouseButtonCount; ++b) {
            CHECK(ParseMouseButton(MouseButtonName(static_cast<MouseButton>(b))) == static_cast<MouseButton>(b));
        }
        CHECK(std::string(MouseButtonName(MouseButton::Forward)) == "MouseForward");
        InputActions back;
        CHECK(back.FromJson(actions.ToJson()) && back == actions);  // o botão lateral vai e volta no JSON
    }

    SECTION("Texto digitado");
    {
        Input::BeginFrame();
        CHECK(Input::TextInput().empty());
        Input::AddTextInput("ç");
        Input::AddTextInput("ão!");
        CHECK(Input::TextInput() == "ção!");
        Input::BeginFrame();
        CHECK(Input::TextInput().empty());
    }

    SECTION("Zona morta radial");
    {
        CHECK(Near(ShapeStick({0.1f, -0.1f}, 0.2f), {0.0f, 0.0f}));  // dentro da zona
        CHECK(Near(ShapeStick({0.6f, 0.0f}, 0.2f), {0.5f, 0.0f}));   // (0.6 - 0.2) / 0.8
        // Diagonal fraca: por eixo, os dois 0.15 sumiriam; radial, o comprimento 0.21 passa e a direção fica.
        const glm::vec2 weak = ShapeStick({0.15f, 0.15f}, 0.2f);
        CHECK(weak.x > 0.0f && Near(weak.x, weak.y));
        const glm::vec2 full = ShapeStick({1.0f, 1.0f}, 0.2f);  // canto do quadrado: limitado ao círculo
        CHECK(Near(Length(full), 1.0f) && Near(full.x, full.y));
        CHECK(Near(ShapeStick({0.0f, 0.0f}, 0.0f), {0.0f, 0.0f}));
        CHECK(Near(ShapeStick({0.0f, -0.5f}, 0.0f), {0.0f, -0.5f}));  // sem zona: como veio

        CHECK(Near(Input::GamepadDeadZone(), Input::kGamepadDeadZone));
        Input::SetGamepadDeadZone(0.35f);
        CHECK(Near(Input::GamepadDeadZone(), 0.35f));
        Input::SetGamepadDeadZone(2.0f);
        CHECK(Near(Input::GamepadDeadZone(), 0.9f));
        Input::SetGamepadDeadZone(std::numeric_limits<float>::quiet_NaN());
        CHECK(Near(Input::GamepadDeadZone(), Input::kGamepadDeadZone));
    }

    SECTION("Vários controles: vagas, ligar e desligar");
    {
        Input::BeginFrame();
        Input::SetGamepadSimulated(true, 2);
        CHECK(Input::GamepadCount() == 2 && Input::IsGamepadConnected(0) && Input::IsGamepadConnected(1));
        CHECK(!Input::IsGamepadConnected(2) && !Input::IsGamepadConnected(-1) && !Input::IsGamepadConnected(9));
        CHECK(Input::WasGamepadConnected(0) && Input::WasGamepadConnected(1) && !Input::WasGamepadConnected(2));
        CHECK(Input::GamepadName(0) != Input::GamepadName(1) && Input::GamepadName(3).empty());
        Input::BeginFrame();
        CHECK(!Input::WasGamepadConnected(0) && !Input::WasGamepadConnected(1));

        Input::SetGamepadButtonState(GamepadButton::A, true, 1);
        CHECK(Input::IsGamepadButtonDown(GamepadButton::A));  // qualquer um
        CHECK(!Input::IsGamepadButtonDown(GamepadButton::A, 0) && Input::IsGamepadButtonDown(GamepadButton::A, 1));
        CHECK(Input::WasGamepadButtonPressed(GamepadButton::A, 1) && !Input::WasGamepadButtonPressed(GamepadButton::A, 0));
        CHECK(Input::LastDevice() == InputDevice::Gamepad && Input::LastGamepad() == 1);
        CHECK(Input::GamepadName() == Input::GamepadName(1));  // o último usado

        Input::SetGamepadAxisState(GamepadAxis::LeftX, 0.3f, 0);
        Input::SetGamepadAxisState(GamepadAxis::LeftX, -0.8f, 1);
        CHECK(Near(Input::GamepadAxisValue(GamepadAxis::LeftX), -0.8f));  // o mais empurrado, sem somar
        CHECK(Near(Input::GamepadAxisValue(GamepadAxis::LeftX, 0), 0.3f));
        Input::SetGamepadButtonState(GamepadButton::A, true, 5);  // vaga que não existe: nada
        Input::SetGamepadAxisState(GamepadAxis::LeftX, 0.0f, 0);
        Input::SetGamepadAxisState(GamepadAxis::LeftX, 0.0f, 1);
    }

    SECTION("Ações por jogador");
    {
        InputActions duo;
        duo.actions = {{"Pular", {InputBinding::Of(Key::J), InputBinding::Of(GamepadButton::A)}, InputActionType::Button}};
        CHECK(duo.IsDown("Pular") && duo.IsDown("Pular", 1));  // o A do controle 2 ainda está apertado
        CHECK(!duo.IsDown("Pular", 0) && !duo.IsDown("Pular", kKeyboardMouse));
        Input::SetKeyState(Key::J, true);
        CHECK(duo.IsDown("Pular", kKeyboardMouse) && !duo.IsDown("Pular", 0));
        CHECK(Input::LastDevice() == InputDevice::KeyboardMouse);
        Input::BeginFrame();
        Input::SetGamepadButtonState(GamepadButton::A, true, 0);
        CHECK(duo.WasPressed("Pular", 0) && !duo.WasPressed("Pular", 1) && !duo.WasPressed("Pular"));
        Input::SetKeyState(Key::J, false);
        Input::SetGamepadButtonState(GamepadButton::A, false, 0);
        Input::BeginFrame();
    }

    SECTION("Controle desligado solta o que estava apertado");
    {
        CHECK(Input::IsGamepadButtonDown(GamepadButton::A, 1));
        Input::SetGamepadSimulated(true, 1);  // o controle 2 sai
        CHECK(Input::WasGamepadDisconnected(1) && !Input::IsGamepadConnected(1) && Input::IsGamepadConnected(0));
        CHECK(Input::WasGamepadButtonReleased(GamepadButton::A, 1) && !Input::IsGamepadButtonDown(GamepadButton::A));
        CHECK(Input::LastGamepad() == 0 && Input::GamepadCount() == 1);  // o último usado foi o 1, não o 2
        Input::BeginFrame();
        CHECK(!Input::WasGamepadDisconnected(1) && !Input::WasGamepadButtonReleased(GamepadButton::A, 1));
        Input::SetGamepadSimulated(false);
        CHECK(Input::GamepadCount() == 0 && Input::WasGamepadDisconnected(0) && !Input::IsGamepadSimulated());
        Input::BeginFrame();
    }

    SECTION("Último dispositivo: o mouse só conta andando de verdade");
    {
        Input::SetGamepadSimulated(true);
        Input::SetGamepadButtonState(GamepadButton::B, true);
        CHECK(Input::LastDevice() == InputDevice::Gamepad && Input::LastGamepad() == 0);
        Input::SetMousePosition({300.0f, 300.0f});
        Input::SetMousePosition({302.0f, 301.0f});  // esbarrão
        CHECK(Input::LastDevice() == InputDevice::Gamepad);
        Input::SetMousePosition({340.0f, 310.0f});
        CHECK(Input::LastDevice() == InputDevice::KeyboardMouse && Input::LastGamepad() == 0);
        Input::SetGamepadSimulated(false);
        Input::BeginFrame();
    }

    return TestResult();
}
