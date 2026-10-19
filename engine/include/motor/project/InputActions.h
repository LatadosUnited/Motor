#pragma once

#include "motor/core/Input.h"

#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

// Uma entrada ligada a uma ação: tecla, botão do mouse, botão do controle ou eixo do controle. `scale` dá o
// sentido nos eixos: a tecla A em "Horizontal" vale -1, a D vale +1; um analógico vale o valor dele vezes `scale`.
struct InputBinding {
    enum class Source { Key, Mouse, GamepadButton, GamepadAxis };
    Source source = Source::Key;
    int code = 0;  // o valor de Key, MouseButton, GamepadButton ou GamepadAxis
    float scale = 1.0f;

    static InputBinding Of(Key key, float scale = 1.0f) { return {Source::Key, static_cast<int>(key), scale}; }
    static InputBinding Of(MouseButton b, float scale = 1.0f) { return {Source::Mouse, static_cast<int>(b), scale}; }
    static InputBinding Of(GamepadButton b, float scale = 1.0f) {
        return {Source::GamepadButton, static_cast<int>(b), scale};
    }
    static InputBinding Of(GamepadAxis a, float scale = 1.0f) { return {Source::GamepadAxis, static_cast<int>(a), scale}; }

    std::string Label() const;  // "Tecla Space", "Controle A", "Analógico LeftX (-)"...
    bool operator==(const InputBinding&) const = default;
};

struct InputAction {
    std::string name;
    std::vector<InputBinding> bindings;
    bool operator==(const InputAction&) const = default;
};

// Mapa de ações do projeto (Fase 21): os scripts perguntam por "Pular" ou "Horizontal" em vez de uma tecla, e o
// mesmo código funciona no teclado e no controle. Fica no .mproject ("input"); Arquivo > Controles edita.
//
// Valor de uma ação: a soma das entradas (tecla/botão apertado = `scale`, eixo = valor * `scale`), limitada a
// -1..1. Apertada: valor > 0.5 (um eixo empurrado mais da metade conta como botão).
class InputActions {
public:
    std::vector<InputAction> actions;

    // As do projeto novo (e de projeto sem mapa): Horizontal, Vertical, Olhar X/Y, Pular, Correr, Acao, Pausa.
    static InputActions Defaults();

    const InputAction* Find(std::string_view name) const;  // sem diferença de maiúsculas

    // Lidos do motor::Input (o filtro de foco é de quem chama: Script).
    float Value(std::string_view name) const;
    bool IsDown(std::string_view name) const { return Value(name) > 0.5f; }
    bool WasPressed(std::string_view name) const;   // apertou neste frame
    bool WasReleased(std::string_view name) const;  // soltou neste frame

    std::string ToJson() const;  // {"actions": [{"name": "Pular", "bindings": [{"key": "Space"}, {"gamepad": "A"}]}]}
    // Entradas com nome desconhecido são puladas (com a mensagem em `error`, mas o resto é lido: devolve true).
    bool FromJson(std::string_view text, std::string* error = nullptr);

    bool operator==(const InputActions& other) const { return actions == other.actions; }

private:
    float ValueOf(const InputAction& action, bool lastFrame) const;
    void WarnUnknown(std::string_view name) const;
    mutable std::set<std::string, std::less<>> m_warned;
};

} // namespace motor
