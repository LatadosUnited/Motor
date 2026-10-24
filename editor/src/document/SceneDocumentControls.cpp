// Documento do editor: mapa de ações do projeto (Arquivo > Controles, Fase 21; v2 na alpha 4). Cada ação ("Pular",
// "Mover") tem um tipo (botão, eixo, vetor 2D) e as suas entradas (teclas, mouse, botões e eixos do controle), com
// escala, lado do vetor e tecla de junto; os scripts perguntam pela ação (Script::IsActionDown, GetAxis,
// GetVector). Gravado no .mproject ("input"); vale a partir do próximo Play.

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
    for (int k = static_cast<int>(kFirstKey); k <= static_cast<int>(kLastKey); ++k) {
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
        for (int k = static_cast<int>(kFirstKey); k <= static_cast<int>(kLastKey); ++k) {
            const char* name = KeyName(static_cast<Key>(k));
            if (*name && ImGui::MenuItem(name)) chosen = InputBinding::Of(static_cast<Key>(k));
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Mouse")) {
        for (int b = 0; b < kMouseButtonCount; ++b) {
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
    if (ImGui::BeginMenu("Movimento do mouse")) {
        for (int a = 0; a < kMouseAxisCount; ++a) {
            if (ImGui::MenuItem(MouseAxisName(static_cast<MouseAxis>(a)))) chosen = InputBinding::Of(static_cast<MouseAxis>(a));
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

constexpr const char* kTypeLabels[] = {"Botão", "Eixo", "Vetor 2D"};

// Teclas de junto que o menu oferece (Left* vale pelos dois lados).
struct WithOption {
    int key;
    const char* label;
};
constexpr WithOption kWithOptions[] = {{0, "Sozinha"},
                                       {static_cast<int>(Key::LeftControl), "Ctrl +"},
                                       {static_cast<int>(Key::LeftShift), "Shift +"},
                                       {static_cast<int>(Key::LeftAlt), "Alt +"}};

// Escala, lado do vetor e tecla de junto de uma entrada. Devolve se mudou.
bool DrawBindingOptions(InputBinding& b, InputActionType type) {
    bool changed = false;
    ImGui::SetNextItemWidth(70.0f);
    if (ImGui::DragFloat("##escala", &b.scale, 0.01f, -10.0f, 10.0f, "%.2f")) changed = true;
    ImGui::SetItemTooltip("Escala: negativa empurra para o lado negativo (A em Horizontal); no mouse e nos "
                          "analógicos é a sensibilidade.");
    if (type == InputActionType::Vector2) {
        ImGui::SameLine();
        int part = b.part == InputBinding::Part::Y ? 1 : 0;
        ImGui::SetNextItemWidth(50.0f);
        if (ImGui::Combo("##lado", &part, "X\0Y\0")) {
            b.part = part == 1 ? InputBinding::Part::Y : InputBinding::Part::X;
            changed = true;
        }
        ImGui::SetItemTooltip("Lado do vetor: em Mover, A e D vão no X, W e S no Y.");
    }
    ImGui::SameLine();
    const auto current = std::find_if(std::begin(kWithOptions), std::end(kWithOptions),
                                      [&](const WithOption& o) { return o.key == b.with; });
    const char* preview = current != std::end(kWithOptions) ? current->label : KeyName(static_cast<Key>(b.with));
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::BeginCombo("##junto", preview)) {
        for (const WithOption& o : kWithOptions) {
            if (ImGui::Selectable(o.label, o.key == b.with)) {
                b.with = o.key;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Tecla que precisa estar apertada junto (Ctrl+S). Vale o lado esquerdo ou o direito.");
    return changed;
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

    ImGui::TextWrapped("Os scripts perguntam pela ação, não pela tecla: WasActionPressed(\"Pular\"), "
                       "GetVector(\"Mover\"), GetAxis(\"Horizontal\"). Vale a partir do próximo Play.");
    if (Input::IsGamepadConnected()) {
        for (int pad = 0; pad < kMaxGamepads; ++pad) {
            if (!Input::IsGamepadConnected(pad)) continue;
            ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "Controle %d conectado: %s%s", pad + 1,
                               Input::GamepadName(pad).c_str(),
                               Input::LastDevice() == InputDevice::Gamepad && Input::LastGamepad() == pad ? " (em uso)" : "");
        }
    } else {
        ImGui::TextDisabled("Nenhum controle conectado (os botões do controle podem ser escolhidos em \"Escolher\").");
    }
    ImGui::Separator();

    int removeAction = -1;
    for (int i = 0; i < static_cast<int>(actions.actions.size()); ++i) {
        InputAction& action = actions.actions[static_cast<size_t>(i)];
        ImGui::PushID(i);
        // Valor agora (teclado, mouse e controle), para conferir o mapa sem dar Play.
        const glm::vec2 value = InputActions::VectorOf(action);
        const bool open = ImGui::TreeNodeEx("##acao", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap,
                                            "%s", action.name.c_str());
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 190.0f);
        if (action.type == InputActionType::Vector2) {
            ImGui::Text("%+.2f, %+.2f", value.x, value.y);
        } else {
            const float bar = action.type == InputActionType::Button ? value.x : (value.x + 1.0f) * 0.5f;
            ImGui::ProgressBar(std::clamp(bar, 0.0f, 1.0f), ImVec2(120.0f, 0.0f), std::format("{:+.2f}", value.x).c_str());
        }
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 55.0f);
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
            ImGui::SameLine();
            int type = static_cast<int>(action.type);
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::Combo("Tipo", &type, kTypeLabels, IM_ARRAYSIZE(kTypeLabels))) {
                action.type = static_cast<InputActionType>(type);
                changed = true;
            }
            ImGui::SetItemTooltip("Botão: 0 a 1 (Pular). Eixo: -1 a 1, GetAxis (Horizontal). Vetor 2D: X e Y, "
                                  "GetVector, com a diagonal limitada (Mover, Olhar).");
            int removeBinding = -1;
            for (int j = 0; j < static_cast<int>(action.bindings.size()); ++j) {
                InputBinding& b = action.bindings[static_cast<size_t>(j)];
                ImGui::PushID(j);
                ImGui::Bullet();
                if (DrawBindingOptions(b, action.type)) changed = true;
                ImGui::SameLine();
                ImGui::TextUnformatted(b.Label().c_str());
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
    ImGui::SetItemTooltip("Mover, Olhar, Horizontal, Vertical, Olhar X/Y, Pular, Correr, Interagir e Pausa, com "
                          "teclado, mouse e controle.");

    if (changed && !(actions == m_project->Actions())) {
        std::string error;
        if (!SetInputActions(actions, error)) ShowError("Não foi possível salvar os controles: " + error);
    }
    ImGui::End();
}

} // namespace editor
