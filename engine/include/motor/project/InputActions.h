#pragma once

#include "motor/core/Input.h"

#include <glm/vec2.hpp>

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

// Movimento do mouse como eixo: X e Y (velocidade, Y para cima positivo) e a roda (entalhes no quadro).
enum class MouseAxis : int { X = 0, Y, Scroll };
inline constexpr int kMouseAxisCount = 3;
// O mouse a esta velocidade (pixels por segundo) vale 1 numa entrada de `scale` 1, como o analógico encostado.
inline constexpr float kMouseAxisPixelsPerSecond = 1000.0f;
const char* MouseAxisName(MouseAxis axis);  // "MouseX", "MouseY", "MouseScroll"
std::optional<MouseAxis> ParseMouseAxis(std::string_view name);

// Uma entrada ligada a uma ação: tecla, botão do mouse, movimento do mouse, botão do controle ou eixo do
// controle. `scale` dá o sentido e a sensibilidade: a tecla A em "Horizontal" vale -1, a D vale +1; um eixo vale o
// valor dele vezes `scale`. `with` é uma tecla que precisa estar apertada junto (Ctrl+S); Left* vale também pelo
// lado direito. `part` diz a que lado de uma ação Vector2 a entrada vai (W em "Mover" vai no Y).
struct InputBinding {
    enum class Source { Key, Mouse, GamepadButton, GamepadAxis, MouseAxis };
    enum class Part { X, Y };
    Source source = Source::Key;
    int code = 0;  // o valor de Key, MouseButton, GamepadButton, GamepadAxis ou MouseAxis
    float scale = 1.0f;
    Part part = Part::X;
    int with = 0;  // código de Key; 0 = sem tecla junto

    static InputBinding Of(Key key, float scale = 1.0f) { return {Source::Key, static_cast<int>(key), scale}; }
    static InputBinding Of(MouseButton b, float scale = 1.0f) { return {Source::Mouse, static_cast<int>(b), scale}; }
    static InputBinding Of(GamepadButton b, float scale = 1.0f) {
        return {Source::GamepadButton, static_cast<int>(b), scale};
    }
    static InputBinding Of(GamepadAxis a, float scale = 1.0f) { return {Source::GamepadAxis, static_cast<int>(a), scale}; }
    static InputBinding Of(MouseAxis a, float scale = 1.0f) { return {Source::MouseAxis, static_cast<int>(a), scale}; }
    InputBinding Y() const {  // vai no Y de uma ação Vector2
        InputBinding b = *this;
        b.part = Part::Y;
        return b;
    }
    InputBinding With(Key key) const {  // só vale com `key` apertada junto
        InputBinding b = *this;
        b.with = static_cast<int>(key);
        return b;
    }
    // O mouse dá velocidade, sem limite: soma por fora do limite de -1..1 das outras entradas.
    bool IsRelative() const { return source == Source::MouseAxis && code != static_cast<int>(MouseAxis::Scroll); }

    std::string Label() const;  // "Tecla Space", "LeftControl + Tecla S", "Analógico LeftX (-)", "Mouse MouseY (Y)"...
    bool operator==(const InputBinding&) const = default;
};

// Botão: 0 a 1 (pular, atirar). Eixo: -1 a 1 (Horizontal). Vector2: X e Y, com a diagonal limitada ao círculo
// (W+D não anda mais rápido que W).
enum class InputActionType { Button, Axis, Vector2 };
const char* InputActionTypeName(InputActionType type);  // "button", "axis", "vector2"

struct InputAction {
    std::string name;
    std::vector<InputBinding> bindings;
    InputActionType type = InputActionType::Axis;
    bool operator==(const InputAction&) const = default;
};

// Mapa de ações do projeto (Fase 21, v2 na alpha 4): os scripts perguntam por "Pular" ou "Mover" em vez de uma
// tecla, e o mesmo código funciona no teclado, no mouse e no controle. Fica no .mproject ("input"); Arquivo >
// Controles edita.
//
// Valor de uma ação: a soma das entradas (tecla/botão apertado = `scale`, eixo = valor * `scale`), limitada a
// -1..1 (Botão: 0..1; Vector2: ao círculo de raio 1); o movimento do mouse soma depois do limite. Apertada:
// valor > 0.5 (um eixo empurrado mais da metade conta como botão; num Vector2, o comprimento).
class InputActions {
public:
    std::vector<InputAction> actions;

    // As do projeto novo (e de projeto sem mapa): Mover, Olhar, Horizontal, Vertical, Olhar X/Y, Pular, Correr,
    // Interagir, Pausa.
    static InputActions Defaults();

    const InputAction* Find(std::string_view name) const;  // sem diferença de maiúsculas

    // Lidos do motor::Input (o filtro de foco é de quem chama: Script).
    float Value(std::string_view name) const;       // num Vector2, o comprimento
    glm::vec2 Vector(std::string_view name) const;  // num Botão ou Eixo, (valor, 0)
    bool IsDown(std::string_view name) const { return Value(name) > 0.5f; }
    bool WasPressed(std::string_view name) const;   // apertou neste frame
    bool WasReleased(std::string_view name) const;  // soltou neste frame

    // Valor de uma ação que não está no mapa (prévia do editor).
    static glm::vec2 VectorOf(const InputAction& action);

    // {"version": 2, "actions": [{"name": "Pular", "type": "button", "bindings": [{"key": "Space"}, {"gamepad": "A"}]}]}
    std::string ToJson() const;
    // Lê também a versão 1 (sem "type": tudo eixo). Entradas com nome desconhecido são puladas (com a mensagem em
    // `error`, mas o resto é lido: devolve true).
    bool FromJson(std::string_view text, std::string* error = nullptr);

    bool operator==(const InputActions& other) const { return actions == other.actions; }

private:
    const InputAction* FindOrWarn(std::string_view name) const;
    mutable std::set<std::string, std::less<>> m_warned;
};

} // namespace motor
