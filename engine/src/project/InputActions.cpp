#include "motor/project/InputActions.h"

#include "motor/core/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

namespace motor {

using json = nlohmann::ordered_json;

namespace {

constexpr int kFormatVersion = 2;

bool SameName(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

constexpr const char* kMouseAxisNames[kMouseAxisCount] = {"MouseX", "MouseY", "MouseScroll"};
constexpr InputActionType kTypes[] = {InputActionType::Button, InputActionType::Axis, InputActionType::Vector2};

bool KeyHeld(Key key, bool lastFrame) {
    return lastFrame ? Input::WasKeyDownLastFrame(key) : Input::IsKeyDown(key);
}

// A tecla de junto: Left* aceita também o lado direito (Ctrl é Ctrl dos dois lados).
bool WithHeld(int with, bool lastFrame) {
    if (with == 0) return true;
    const Key key = static_cast<Key>(with);
    if (KeyHeld(key, lastFrame)) return true;
    constexpr int kLeftToRight = static_cast<int>(Key::RightShift) - static_cast<int>(Key::LeftShift);
    if (with >= static_cast<int>(Key::LeftShift) && with <= static_cast<int>(Key::LeftSuper)) {
        return KeyHeld(static_cast<Key>(with + kLeftToRight), lastFrame);
    }
    return false;
}

bool FromKeyboardMouse(const InputBinding& b) {
    return b.source == InputBinding::Source::Key || b.source == InputBinding::Source::Mouse ||
           b.source == InputBinding::Source::MouseAxis;
}

// Valor de uma entrada (sem o `scale`): 1/0 para teclas e botões, o eixo como está, o mouse na medida do analógico.
// `device` filtra de onde (ver InputActions::Value).
float RawValue(const InputBinding& b, bool lastFrame, const InputFilter& filter) {
    const int device = filter.device;
    const bool keyboardMouse = FromKeyboardMouse(b);
    if (device >= 0 && keyboardMouse) return 0.0f;
    if (device == kKeyboardMouse && !keyboardMouse) return 0.0f;
    if (filter.uiHasMouse && b.source == InputBinding::Source::Mouse && b.code == static_cast<int>(MouseButton::Left)) {
        return 0.0f;
    }
    if (!WithHeld(b.with, lastFrame)) return 0.0f;
    switch (b.source) {
        case InputBinding::Source::Key: return KeyHeld(static_cast<Key>(b.code), lastFrame) ? 1.0f : 0.0f;
        case InputBinding::Source::Mouse: {
            const MouseButton button = static_cast<MouseButton>(b.code);
            return (lastFrame ? Input::WasMouseDownLastFrame(button) : Input::IsMouseDown(button)) ? 1.0f : 0.0f;
        }
        case InputBinding::Source::GamepadButton: {
            const GamepadButton button = static_cast<GamepadButton>(b.code);
            const int pad = device >= 0 ? device : kAnyDevice;
            return (lastFrame ? Input::WasGamepadButtonDownLastFrame(button, pad) : Input::IsGamepadButtonDown(button, pad))
                       ? 1.0f
                       : 0.0f;
        }
        case InputBinding::Source::GamepadAxis: {
            const GamepadAxis axis = static_cast<GamepadAxis>(b.code);
            const int pad = device >= 0 ? device : kAnyDevice;
            return lastFrame ? Input::GamepadAxisValueLastFrame(axis, pad) : Input::GamepadAxisValue(axis, pad);
        }
        case InputBinding::Source::MouseAxis: {
            if (b.code == static_cast<int>(MouseAxis::Scroll)) {
                return lastFrame ? Input::ScrollDeltaLastFrame() : Input::ScrollDelta();
            }
            const glm::vec2 v = lastFrame ? Input::MouseVelocityLastFrame() : Input::MouseVelocity();
            // Y da tela é para baixo; nas ações, como nos analógicos, para cima é positivo.
            return (b.code == static_cast<int>(MouseAxis::X) ? v.x : -v.y) / kMouseAxisPixelsPerSecond;
        }
    }
    return 0.0f;
}

glm::vec2 VectorAt(const InputAction& action, bool lastFrame, const InputFilter& filter) {
    if (!InputActions::IsContextActive(action, filter)) return glm::vec2{0.0f};
    glm::vec2 limited{0.0f};
    glm::vec2 relative{0.0f};
    for (const InputBinding& b : action.bindings) {
        const float v = RawValue(b, lastFrame, filter) * b.scale;
        glm::vec2& sum = b.IsRelative() ? relative : limited;
        if (action.type == InputActionType::Vector2 && b.part == InputBinding::Part::Y) {
            sum.y += v;
        } else {
            sum.x += v;
        }
    }
    switch (action.type) {
        case InputActionType::Button: return {std::clamp(limited.x + relative.x, 0.0f, 1.0f), 0.0f};
        case InputActionType::Axis: return {std::clamp(limited.x, -1.0f, 1.0f) + relative.x, 0.0f};
        case InputActionType::Vector2: {
            const float length = std::sqrt(limited.x * limited.x + limited.y * limited.y);
            if (length > 1.0f) limited /= length;
            return limited + relative;
        }
    }
    return {};
}

float ValueAt(const InputAction& action, bool lastFrame, const InputFilter& filter) {
    const glm::vec2 v = VectorAt(action, lastFrame, filter);
    return action.type == InputActionType::Vector2 ? std::sqrt(v.x * v.x + v.y * v.y) : v.x;
}

} // namespace

const char* MouseAxisName(MouseAxis axis) {
    const int a = static_cast<int>(axis);
    return a >= 0 && a < kMouseAxisCount ? kMouseAxisNames[a] : "";
}

std::optional<MouseAxis> ParseMouseAxis(std::string_view name) {
    for (int a = 0; a < kMouseAxisCount; ++a) {
        if (SameName(kMouseAxisNames[a], name)) return static_cast<MouseAxis>(a);
    }
    return std::nullopt;
}

const char* InputActionTypeName(InputActionType type) {
    switch (type) {
        case InputActionType::Button: return "button";
        case InputActionType::Axis: return "axis";
        case InputActionType::Vector2: return "vector2";
    }
    return "";
}

std::string InputBinding::Label() const {
    std::string text = with != 0 ? std::format("{} + ", KeyName(static_cast<Key>(with))) : std::string();
    switch (source) {
        case Source::Key: text += std::format("Tecla {}", KeyName(static_cast<Key>(code))); break;
        case Source::Mouse: text += std::format("Mouse {}", MouseButtonName(static_cast<MouseButton>(code))); break;
        case Source::GamepadButton:
            text += std::format("Controle {}", GamepadButtonName(static_cast<GamepadButton>(code)));
            break;
        case Source::GamepadAxis:
            text += std::format("Analógico {}", GamepadAxisName(static_cast<GamepadAxis>(code)));
            break;
        case Source::MouseAxis: text += std::format("Mouse {}", MouseAxisName(static_cast<MouseAxis>(code))); break;
    }
    if (scale < 0.0f) text += " (-)";
    return text;
}

InputActions InputActions::Defaults() {
    using B = InputBinding;
    using T = InputActionType;
    InputActions a;
    // Contexto Jogo: andar, olhar e agir. Contexto Menu: navegar nas telas (o script troca ao abrir e fechar).
    InputAction mover{"Mover",
                      {B::Of(Key::D), B::Of(Key::A, -1), B::Of(Key::W).Y(), B::Of(Key::S, -1).Y(), B::Of(Key::Right),
                       B::Of(Key::Left, -1), B::Of(Key::Up).Y(), B::Of(Key::Down, -1).Y(), B::Of(GamepadAxis::LeftX),
                       B::Of(GamepadAxis::LeftY).Y(), B::Of(GamepadButton::DpadRight), B::Of(GamepadButton::DpadLeft, -1),
                       B::Of(GamepadButton::DpadUp).Y(), B::Of(GamepadButton::DpadDown, -1).Y()},
                      T::Vector2, "Jogo"};
    InputAction navegar = mover;
    navegar.name = "Navegar";
    navegar.context = "Menu";
    a.actions = {
        mover,
        {"Olhar",
         {B::Of(MouseAxis::X), B::Of(MouseAxis::Y).Y(), B::Of(GamepadAxis::RightX), B::Of(GamepadAxis::RightY).Y()},
         T::Vector2, "Jogo"},
        {"Horizontal",
         {B::Of(Key::D), B::Of(Key::A, -1), B::Of(Key::Right), B::Of(Key::Left, -1), B::Of(GamepadAxis::LeftX),
          B::Of(GamepadButton::DpadRight), B::Of(GamepadButton::DpadLeft, -1)},
         T::Axis, "Jogo"},
        {"Vertical",
         {B::Of(Key::W), B::Of(Key::S, -1), B::Of(Key::Up), B::Of(Key::Down, -1), B::Of(GamepadAxis::LeftY),
          B::Of(GamepadButton::DpadUp), B::Of(GamepadButton::DpadDown, -1)},
         T::Axis, "Jogo"},
        {"Olhar X", {B::Of(MouseAxis::X), B::Of(GamepadAxis::RightX)}, T::Axis, "Jogo"},
        {"Olhar Y", {B::Of(MouseAxis::Y), B::Of(GamepadAxis::RightY)}, T::Axis, "Jogo"},
        {"Pular", {B::Of(Key::Space), B::Of(GamepadButton::A)}, T::Button, "Jogo"},
        {"Correr", {B::Of(Key::LeftShift), B::Of(Key::RightShift), B::Of(GamepadButton::LeftThumb)}, T::Button, "Jogo"},
        {"Interagir", {B::Of(Key::E), B::Of(GamepadButton::X)}, T::Button, "Jogo"},
        {"Pausa", {B::Of(Key::Escape), B::Of(GamepadButton::Start)}, T::Button, "Jogo"},
        navegar,
        {"Confirmar", {B::Of(Key::Enter), B::Of(Key::Space), B::Of(GamepadButton::A)}, T::Button, "Menu"},
        {"Voltar", {B::Of(Key::Escape), B::Of(GamepadButton::B), B::Of(GamepadButton::Start)}, T::Button, "Menu"},
    };
    return a;
}

const InputAction* InputActions::Find(std::string_view name) const {
    for (const InputAction& action : actions) {
        if (SameName(action.name, name)) return &action;
    }
    return nullptr;
}

const InputAction* InputActions::FindOrWarn(std::string_view name) const {
    const InputAction* action = Find(name);
    if (!action && !m_warned.contains(name)) {
        m_warned.emplace(name);
        log::Warn("Ação de entrada desconhecida: \"{}\" (veja Arquivo > Controles)", name);
    }
    return action;
}

glm::vec2 InputActions::VectorOf(const InputAction& action) {
    return VectorAt(action, false, {});
}

bool InputActions::IsContextActive(const InputAction& action, const InputFilter& filter) {
    if (action.context.empty() || !filter.contexts) return true;
    return std::any_of(filter.contexts->begin(), filter.contexts->end(),
                       [&](const std::string& c) { return SameName(c, action.context); });
}

float InputActions::Value(std::string_view name, const InputFilter& filter) const {
    const InputAction* action = FindOrWarn(name);
    return action ? ValueAt(*action, false, filter) : 0.0f;
}

glm::vec2 InputActions::Vector(std::string_view name, const InputFilter& filter) const {
    const InputAction* action = FindOrWarn(name);
    return action ? VectorAt(*action, false, filter) : glm::vec2{0.0f};
}

// O "antes" usa os contextos de agora: uma tecla segurada desde antes de o contexto ligar não vira um aperto
// novo (o Espaço de Pular segurado não confirma o menu que acabou de abrir).
bool InputActions::WasPressed(std::string_view name, const InputFilter& filter) const {
    const InputAction* action = FindOrWarn(name);
    return action && ValueAt(*action, false, filter) > 0.5f && ValueAt(*action, true, filter) <= 0.5f;
}

bool InputActions::WasReleased(std::string_view name, const InputFilter& filter) const {
    const InputAction* action = FindOrWarn(name);
    return action && ValueAt(*action, false, filter) <= 0.5f && ValueAt(*action, true, filter) > 0.5f;
}

std::string InputActions::ToJson() const {
    json list = json::array();
    for (const InputAction& action : actions) {
        json bindings = json::array();
        for (const InputBinding& b : action.bindings) {
            json entry;
            switch (b.source) {
                case InputBinding::Source::Key: entry["key"] = KeyName(static_cast<Key>(b.code)); break;
                case InputBinding::Source::Mouse: entry["mouse"] = MouseButtonName(static_cast<MouseButton>(b.code)); break;
                case InputBinding::Source::GamepadButton:
                    entry["gamepad"] = GamepadButtonName(static_cast<GamepadButton>(b.code));
                    break;
                case InputBinding::Source::GamepadAxis:
                    entry["axis"] = GamepadAxisName(static_cast<GamepadAxis>(b.code));
                    break;
                case InputBinding::Source::MouseAxis: entry["mouse"] = MouseAxisName(static_cast<MouseAxis>(b.code)); break;
            }
            if (b.scale != 1.0f) entry["scale"] = b.scale;
            if (b.with != 0) entry["with"] = KeyName(static_cast<Key>(b.with));
            if (b.part == InputBinding::Part::Y) entry["part"] = "y";
            bindings.push_back(std::move(entry));
        }
        json entry{{"name", action.name}, {"type", InputActionTypeName(action.type)}};
        if (!action.context.empty()) entry["context"] = action.context;
        entry["bindings"] = std::move(bindings);
        list.push_back(std::move(entry));
    }
    return json{{"version", kFormatVersion}, {"startContext", startContext}, {"actions", std::move(list)}}.dump(
        2, ' ', false, json::error_handler_t::replace);
}

bool InputActions::FromJson(std::string_view text, std::string* error) {
    std::vector<InputAction> read;
    std::string start = "Jogo";
    std::string problems;
    const auto problem = [&](std::string_view action, std::string_view what) {
        problems += std::format("{}{}: {}", problems.empty() ? "" : "; ", action, what);
    };
    try {
        const json root = json::parse(text);
        const auto list = root.find("actions");
        if (!root.is_object() || list == root.end() || !list->is_array()) {
            if (error) *error = "o mapa de ações precisa de uma lista \"actions\"";
            return false;
        }
        if (const auto value = root.find("startContext"); value != root.end() && value->is_string()) {
            start = value->get<std::string>();
        }
        for (const json& a : *list) {
            if (!a.is_object() || !a.contains("name") || !a["name"].is_string()) continue;
            InputAction action;
            action.name = a["name"].get<std::string>();
            if (const auto context = a.find("context"); context != a.end() && context->is_string()) {
                action.context = context->get<std::string>();
            }
            if (action.name.empty() || std::any_of(read.begin(), read.end(), [&](const InputAction& o) {
                    return SameName(o.name, action.name);
                })) {
                continue;  // sem nome ou repetida
            }
            if (const auto type = a.find("type"); type != a.end() && type->is_string()) {
                const std::string name = type->get<std::string>();
                const auto* found = std::find_if(std::begin(kTypes), std::end(kTypes),
                                                 [&](InputActionType t) { return SameName(InputActionTypeName(t), name); });
                if (found != std::end(kTypes)) {
                    action.type = *found;
                } else {
                    problem(action.name, std::format("tipo desconhecido '{}' (lida como eixo)", name));
                }
            }
            for (const json& b : a.value("bindings", json::array())) {
                if (!b.is_object()) continue;
                InputBinding binding;
                bool ok = false;
                std::string what;
                if (b.contains("key") && b["key"].is_string()) {
                    what = b["key"].get<std::string>();
                    if (const auto k = ParseKeyName(what)) binding = InputBinding::Of(*k), ok = true;
                } else if (b.contains("mouse") && b["mouse"].is_string()) {
                    what = b["mouse"].get<std::string>();
                    if (const auto m = ParseMouseButton(what)) {
                        binding = InputBinding::Of(*m), ok = true;
                    } else if (const auto m2 = ParseMouseAxis(what)) {
                        binding = InputBinding::Of(*m2), ok = true;
                    }
                } else if (b.contains("gamepad") && b["gamepad"].is_string()) {
                    what = b["gamepad"].get<std::string>();
                    if (const auto g = ParseGamepadButton(what)) binding = InputBinding::Of(*g), ok = true;
                } else if (b.contains("axis") && b["axis"].is_string()) {
                    what = b["axis"].get<std::string>();
                    if (const auto x = ParseGamepadAxis(what)) binding = InputBinding::Of(*x), ok = true;
                }
                if (!ok) {
                    problem(action.name, std::format("entrada desconhecida '{}'", what.empty() ? b.dump() : what));
                    continue;
                }
                binding.scale = b.value("scale", 1.0f);
                if (!std::isfinite(binding.scale)) binding.scale = 1.0f;
                if (const auto part = b.find("part"); part != b.end() && part->is_string() && SameName(part->get<std::string>(), "y")) {
                    binding.part = InputBinding::Part::Y;
                }
                if (const auto with = b.find("with"); with != b.end() && with->is_string()) {
                    if (const auto k = ParseKeyName(with->get<std::string>())) {
                        binding.with = static_cast<int>(*k);
                    } else {
                        // Sem a tecla de junto, a entrada valeria sozinha (Ctrl+S viraria S): fica de fora.
                        problem(action.name, std::format("tecla de junto desconhecida '{}'", with->get<std::string>()));
                        continue;
                    }
                }
                action.bindings.push_back(binding);
            }
            read.push_back(std::move(action));
        }
    } catch (const json::exception& e) {
        if (error) *error = std::format("mapa de ações inválido: {}", e.what());
        return false;
    }
    actions = std::move(read);
    startContext = std::move(start);
    m_warned.clear();
    if (error) *error = problems;
    return true;
}

} // namespace motor
