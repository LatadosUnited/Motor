// Documento do editor: mapa de ações do projeto (Arquivo > Controles, Fase 21). Cada ação ("Pular", "Horizontal")
// tem as suas entradas (teclas, mouse, botões e eixos do controle); os scripts perguntam pela ação
// (Script::IsActionDown, GetAxis). Gravado no .mproject ("input"); vale a partir do próximo Play.

#include "document/SceneDocument.h"

#include <motor/core/Input.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string>

namespace editor {

using namespace motor;

namespace {

// Primeira entrada apertada neste frame (tecla, botão do controle ou eixo empurrado), para "aperte para ligar".
std::optional<InputBinding> PressedBinding() {
    for (int k = static_cast<int>(Key::Space); k <= static_cast<int>(Key::RightSuper); ++k) {
        const Key key = static_cast<Key>(k);
        if (*KeyName(key) && Input::WasKeyPressed(key)) return InputBinding::Of(key);
    }
    for (int b = 0; b < kGamepadButtonCount; ++b) {
        if (Input::WasGamepadButtonPressed(static_cast<GamepadButton>(b))) return InputBinding::Of(static_cast<GamepadButton>(b));
    }
    for (int a = 0; a < kGamepadAxisCount; ++a) {
        const float v = Input::GamepadAxisValue(static_cast<GamepadAxis>(a));
        if (std::abs(v) > 0.6f) return InputBinding::Of(static_cast<GamepadAxis>(a), v < 0.0f ? -1.0f : 1.0f);
    }
    return std::nullopt;
}

// Menu "Escolher": todas as entradas por tipo (sem controle conectado, é o jeito de ligar um botão dele).
std::optional<InputBinding> ChooseBindingMenu() {
    std::optional<InputBinding> chosen;
    if (ImGui::BeginMenu("Teclado")) {
        for (int k = static_cast<int>(Key::Space); k <= static_cast<int>(Key::RightSuper); ++k) {
            const char* name = KeyName(static_cast<Key>(k));
            if (*name && ImGui::MenuItem(name)) chosen = InputBinding::Of(static_cast<Key>(k));
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Mouse")) {
        for (int b = 0; b < 3; ++b) {
            if (ImGui::MenuItem(MouseButtonName(static_cast<MouseButton>(b)))) chosen = InputBinding::Of(static_cast<MouseButton>(b));
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Botão do controle")) {
        for (int b = 0; b < kGamepadButtonCount; ++b) {
            if (ImGui::MenuItem(GamepadButtonName(static_cast<GamepadButton>(b)))) {
                chosen = InputBinding::Of(static_cast<GamepadButton>(b));
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Eixo do controle")) {
        for (int a = 0; a < kGamepadAxisCount; ++a) {
            if (ImGui::MenuItem(GamepadAxisName(static_cast<GamepadAxis>(a)))) chosen = InputBinding::Of(static_cast<GamepadAxis>(a));
        }
        ImGui::EndMenu();
    }
    return chosen;
}

} // namespace

bool SceneDocument::SetInputActions(const InputActions& actions, std::string& error) {
    if (!m_project) {
        error = "nenhum projeto aberto";
        return false;
    }
    const InputActions before = m_project->Actions();
    m_project->SetActions(actions);
    if (!m_project->Save(&error)) {
        m_project->SetActions(before);
        return false;
    }
    return true;
}

void SceneDocument::DrawControlsWindow() {
    if (!m_showControlsWindow) {
        m_listeningAction = -1;
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(520.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Controles", &m_showControlsWindow)) {
        ImGui::End();
        return;
    }
    if (!m_project) {
        ImGui::TextDisabled("Abra um projeto para editar os controles.");
        ImGui::End();
        return;
    }

    InputActions actions = m_project->Actions();
    bool changed = false;

    ImGui::TextWrapped("Os scripts perguntam pela ação, não pela tecla: IsActionDown(\"Pular\"), "
                       "WasActionPressed(\"Pular\"), GetAxis(\"Horizontal\"). Vale a partir do próximo Play.");
    if (Input::IsGamepadConnected()) {
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "Controle conectado: %s", Input::GamepadName().c_str());
    } else {
        ImGui::TextDisabled("Nenhum controle conectado (os botões do controle podem ser escolhidos em \"Escolher\").");
    }
    ImGui::Separator();

    int removeAction = -1;
    for (int i = 0; i < static_cast<int>(actions.actions.size()); ++i) {
        InputAction& action = actions.actions[static_cast<size_t>(i)];
        ImGui::PushID(i);
        // Valor agora (teclado e controle), para conferir o mapa sem dar Play.
        float value = 0.0f;
        for (const InputBinding& b : action.bindings) {
            InputActions single;
            single.actions = {{action.name, {b}}};
            value += single.Value(action.name);
        }
        value = std::clamp(value, -1.0f, 1.0f);
        const bool open = ImGui::TreeNodeEx("##acao", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap,
                                            "%s", action.name.c_str());
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 150.0f);
        ImGui::ProgressBar((value + 1.0f) * 0.5f, ImVec2(80.0f, 0.0f), std::format("{:+.2f}", value).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Excluir")) removeAction = i;
        if (open) {
            std::string name = action.name;
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("Nome", &name);
            if (ImGui::IsItemDeactivatedAfterEdit() && !name.empty() && name != action.name) {
                const InputAction* other = actions.Find(name);
                if (!other || other == &action) {
                    action.name = name;
                    changed = true;
                }
            }
            int removeBinding = -1;
            for (int j = 0; j < static_cast<int>(action.bindings.size()); ++j) {
                InputBinding& b = action.bindings[static_cast<size_t>(j)];
                ImGui::PushID(j);
                ImGui::BulletText("%s", b.Label().c_str());
                ImGui::SameLine();
                bool negative = b.scale < 0.0f;
                if (ImGui::Checkbox("Negativo", &negative)) {
                    b.scale = -b.scale;
                    changed = true;
                }
                ImGui::SetItemTooltip("Em eixos (Horizontal, Vertical): esta entrada empurra para o lado negativo.");
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) removeBinding = j;
                ImGui::PopID();
            }
            if (removeBinding >= 0) {
                action.bindings.erase(action.bindings.begin() + removeBinding);
                changed = true;
            }
            if (m_listeningAction == i) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Aperte uma tecla ou um botão do controle...");
                ImGui::SameLine();
                if (ImGui::SmallButton("Cancelar")) m_listeningAction = -1;
                if (const auto pressed = PressedBinding()) {
                    if (std::find(action.bindings.begin(), action.bindings.end(), *pressed) == action.bindings.end()) {
                        action.bindings.push_back(*pressed);
                        changed = true;
                    }
                    m_listeningAction = -1;
                }
            } else {
                if (ImGui::SmallButton("Adicionar (aperte)")) m_listeningAction = i;
                ImGui::SameLine();
                if (ImGui::SmallButton("Escolher...")) ImGui::OpenPopup("escolher");
                if (ImGui::BeginPopup("escolher")) {
                    if (const auto chosen = ChooseBindingMenu()) {
                        action.bindings.push_back(*chosen);
                        changed = true;
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removeAction >= 0) {
        actions.actions.erase(actions.actions.begin() + removeAction);
        m_listeningAction = -1;
        changed = true;
    }

    ImGui::Separator();
    if (ImGui::Button("Nova ação")) {
        std::string name = "Nova ação";
        for (int n = 2; actions.Find(name); ++n) name = std::format("Nova ação {}", n);
        actions.actions.push_back({name, {}});
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restaurar o padrão")) {
        actions = InputActions::Defaults();
        m_listeningAction = -1;
        changed = true;
    }
    ImGui::SetItemTooltip("Horizontal, Vertical, Olhar X/Y, Pular, Correr, Interagir e Pausa, com teclado e controle.");

    if (changed && !(actions == m_project->Actions())) {
        std::string error;
        if (!SetInputActions(actions, error)) ShowError("Não foi possível salvar os controles: " + error);
    }
    ImGui::End();
}

} // namespace editor
