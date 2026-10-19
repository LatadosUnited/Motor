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

bool SameName(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// Valor de uma entrada (sem o `scale`): 1/0 para teclas e botões, o eixo como está.
float RawValue(const InputBinding& b, bool lastFrame) {
    switch (b.source) {
        case InputBinding::Source::Key: {
            const Key key = static_cast<Key>(b.code);
            return (lastFrame ? Input::WasKeyDownLastFrame(key) : Input::IsKeyDown(key)) ? 1.0f : 0.0f;
        }
        case InputBinding::Source::Mouse: {
            const MouseButton button = static_cast<MouseButton>(b.code);
            return (lastFrame ? Input::WasMouseDownLastFrame(button) : Input::IsMouseDown(button)) ? 1.0f : 0.0f;
        }
        case InputBinding::Source::GamepadButton: {
            const GamepadButton button = static_cast<GamepadButton>(b.code);
            return (lastFrame ? Input::WasGamepadButtonDownLastFrame(button) : Input::IsGamepadButtonDown(button)) ? 1.0f
                                                                                                                 : 0.0f;
        }
        case InputBinding::Source::GamepadAxis: {
            const GamepadAxis axis = static_cast<GamepadAxis>(b.code);
            return lastFrame ? Input::GamepadAxisValueLastFrame(axis) : Input::GamepadAxisValue(axis);
        }
    }
    return 0.0f;
}

} // namespace

std::string InputBinding::Label() const {
    const char* sign = scale < 0.0f ? " (-)" : "";
    switch (source) {
        case Source::Key: return std::format("Tecla {}{}", KeyName(static_cast<Key>(code)), sign);
        case Source::Mouse: return std::format("Mouse {}{}", MouseButtonName(static_cast<MouseButton>(code)), sign);
        case Source::GamepadButton:
            return std::format("Controle {}{}", GamepadButtonName(static_cast<GamepadButton>(code)), sign);
        case Source::GamepadAxis:
            return std::format("Analógico {}{}", GamepadAxisName(static_cast<GamepadAxis>(code)), sign);
    }
    return {};
}

InputActions InputActions::Defaults() {
    using B = InputBinding;
    InputActions a;
    a.actions = {
        {"Horizontal", {B::Of(Key::D), B::Of(Key::A, -1), B::Of(Key::Right), B::Of(Key::Left, -1),
                        B::Of(GamepadAxis::LeftX), B::Of(GamepadButton::DpadRight), B::Of(GamepadButton::DpadLeft, -1)}},
        {"Vertical", {B::Of(Key::W), B::Of(Key::S, -1), B::Of(Key::Up), B::Of(Key::Down, -1), B::Of(GamepadAxis::LeftY),
                      B::Of(GamepadButton::DpadUp), B::Of(GamepadButton::DpadDown, -1)}},
        {"Olhar X", {B::Of(GamepadAxis::RightX)}},
        {"Olhar Y", {B::Of(GamepadAxis::RightY)}},
        {"Pular", {B::Of(Key::Space), B::Of(GamepadButton::A)}},
        {"Correr", {B::Of(Key::LeftShift), B::Of(Key::RightShift), B::Of(GamepadButton::LeftThumb)}},
        {"Interagir", {B::Of(Key::E), B::Of(GamepadButton::X)}},
        {"Pausa", {B::Of(Key::Escape), B::Of(GamepadButton::Start)}},
    };
    return a;
}

const InputAction* InputActions::Find(std::string_view name) const {
    for (const InputAction& action : actions) {
        if (SameName(action.name, name)) return &action;
    }
    return nullptr;
}

void InputActions::WarnUnknown(std::string_view name) const {
    if (m_warned.contains(name)) return;
    m_warned.emplace(name);
    log::Warn("Ação de entrada desconhecida: \"{}\" (veja Arquivo > Controles)", name);
}

float InputActions::ValueOf(const InputAction& action, bool lastFrame) const {
    float sum = 0.0f;
    for (const InputBinding& b : action.bindings) sum += RawValue(b, lastFrame) * b.scale;
    return std::clamp(sum, -1.0f, 1.0f);
}

float InputActions::Value(std::string_view name) const {
    const InputAction* action = Find(name);
    if (!action) {
        WarnUnknown(name);
        return 0.0f;
    }
    return ValueOf(*action, false);
}

bool InputActions::WasPressed(std::string_view name) const {
    const InputAction* action = Find(name);
    if (!action) {
        WarnUnknown(name);
        return false;
    }
    return ValueOf(*action, false) > 0.5f && ValueOf(*action, true) <= 0.5f;
}

bool InputActions::WasReleased(std::string_view name) const {
    const InputAction* action = Find(name);
    if (!action) {
        WarnUnknown(name);
        return false;
    }
    return ValueOf(*action, false) <= 0.5f && ValueOf(*action, true) > 0.5f;
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
            }
            if (b.scale != 1.0f) entry["scale"] = b.scale;
            bindings.push_back(std::move(entry));
        }
        list.push_back({{"name", action.name}, {"bindings", std::move(bindings)}});
    }
    return json{{"actions", std::move(list)}}.dump(2, ' ', false, json::error_handler_t::replace);
}

bool InputActions::FromJson(std::string_view text, std::string* error) {
    std::vector<InputAction> read;
    std::string problems;
    try {
        const json root = json::parse(text);
        const auto list = root.find("actions");
        if (!root.is_object() || list == root.end() || !list->is_array()) {
            if (error) *error = "o mapa de ações precisa de uma lista \"actions\"";
            return false;
        }
        for (const json& a : *list) {
            if (!a.is_object() || !a.contains("name") || !a["name"].is_string()) continue;
            InputAction action;
            action.name = a["name"].get<std::string>();
            if (action.name.empty() || std::any_of(read.begin(), read.end(), [&](const InputAction& o) {
                    return SameName(o.name, action.name);
                })) {
                continue;  // sem nome ou repetida
            }
            for (const json& b : a.value("bindings", json::array())) {
                if (!b.is_object()) continue;
                InputBinding binding;
                binding.scale = b.value("scale", 1.0f);
                if (!std::isfinite(binding.scale)) binding.scale = 1.0f;
                bool ok = false;
                std::string what;
                if (b.contains("key") && b["key"].is_string()) {
                    what = b["key"].get<std::string>();
                    if (const auto k = ParseKeyName(what)) binding = InputBinding::Of(*k, binding.scale), ok = true;
                } else if (b.contains("mouse") && b["mouse"].is_string()) {
                    what = b["mouse"].get<std::string>();
                    if (const auto m = ParseMouseButton(what)) binding = InputBinding::Of(*m, binding.scale), ok = true;
                } else if (b.contains("gamepad") && b["gamepad"].is_string()) {
                    what = b["gamepad"].get<std::string>();
                    if (const auto g = ParseGamepadButton(what)) binding = InputBinding::Of(*g, binding.scale), ok = true;
                } else if (b.contains("axis") && b["axis"].is_string()) {
                    what = b["axis"].get<std::string>();
                    if (const auto x = ParseGamepadAxis(what)) binding = InputBinding::Of(*x, binding.scale), ok = true;
                }
                if (ok) {
                    action.bindings.push_back(binding);
                } else {
                    problems += std::format("{}{}: entrada desconhecida '{}'", problems.empty() ? "" : "; ", action.name,
                                            what.empty() ? b.dump() : what);
                }
            }
            read.push_back(std::move(action));
        }
    } catch (const json::exception& e) {
        if (error) *error = std::format("mapa de ações inválido: {}", e.what());
        return false;
    }
    actions = std::move(read);
    m_warned.clear();
    if (error) *error = problems;
    return true;
}

} // namespace motor
