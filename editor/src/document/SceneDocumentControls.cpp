// Documento do editor: mapa de ações do projeto (Arquivo > Controles, Fase 21; v2 na alpha 4). Cada ação ("Pular",
// "Mover") tem um tipo (botão, eixo, vetor 2D), um contexto (Jogo, Menu) e as suas entradas (teclas, mouse, botões e
// eixos do controle), com lado do vetor, sentido, sensibilidade e tecla de junto; os scripts perguntam pela ação
// (Script::IsActionDown, GetAxis, GetVector). Gravado no .mproject ("input"); vale a partir do próximo Play.
//
// A janela agrupa as ações por contexto (fechadas, com o valor ao vivo no cabeçalho) e mostra as entradas de cada
// uma numa tabela, teclado e mouse antes do controle, com avisos: tecla repetida no mesmo contexto, ação sem
// entradas, vetor sem nada no Y, botão com sentido negativo.

#include "document/SceneDocument.h"

#include <motor/core/Input.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace editor {

using namespace motor;

namespace {

const ImVec4 kWarnColor(1.0f, 0.75f, 0.3f, 1.0f);
const ImVec4 kOkColor(0.5f, 0.9f, 0.5f, 1.0f);
constexpr double kListenSeconds = 6.0;
constexpr const char* kTypeLabels[] = {"Botão", "Eixo", "Vetor 2D"};

bool SameText(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// --- Nomes para a tela (os do arquivo continuam os da engine: "Space", "LeftX") ---------------------------

std::string KeyLabel(Key key) {
    switch (key) {
        case Key::Space: return "Espaço";
        case Key::Escape: return "Esc";
        case Key::Right: return "Seta →";
        case Key::Left: return "Seta ←";
        case Key::Up: return "Seta ↑";
        case Key::Down: return "Seta ↓";
        case Key::LeftShift: return "Shift esq.";
        case Key::RightShift: return "Shift dir.";
        case Key::LeftControl: return "Ctrl esq.";
        case Key::RightControl: return "Ctrl dir.";
        case Key::LeftAlt: return "Alt";
        case Key::RightAlt: return "AltGr";
        case Key::LeftSuper: return "Windows esq.";
        case Key::RightSuper: return "Windows dir.";
        case Key::PageUp: return "Page Up";
        case Key::PageDown: return "Page Down";
        case Key::Delete: return "Delete";
        case Key::Semicolon: return "Ç (;)";
        default: break;
    }
    const int k = static_cast<int>(key);
    if (k >= static_cast<int>(Key::Numpad0) && k <= static_cast<int>(Key::Numpad9)) {
        return std::format("Num {}", k - static_cast<int>(Key::Numpad0));
    }
    std::string name = KeyName(key);
    if (name.starts_with("Numpad")) return "Num " + name.substr(6);
    return name;
}

const char* MouseLabel(MouseButton button) {
    constexpr const char* kLabels[kMouseButtonCount] = {"Mouse esquerdo", "Mouse direito", "Mouse do meio",
                                                        "Mouse lateral (voltar)", "Mouse lateral (avançar)"};
    const int b = static_cast<int>(button);
    return b >= 0 && b < kMouseButtonCount ? kLabels[b] : "";
}

const char* MouseAxisLabel(MouseAxis axis) {
    constexpr const char* kLabels[kMouseAxisCount] = {"Mouse ↔ (mover)", "Mouse ↕ (mover)", "Roda do mouse"};
    const int a = static_cast<int>(axis);
    return a >= 0 && a < kMouseAxisCount ? kLabels[a] : "";
}

const char* PadButtonLabel(GamepadButton button) {
    constexpr const char* kLabels[kGamepadButtonCount] = {
        "A", "B", "X", "Y", "LB", "RB", "Back", "Start", "Guia", "L3 (analógico esq.)", "R3 (analógico dir.)",
        "Direcional ↑", "Direcional →", "Direcional ↓", "Direcional ←"};
    const int b = static_cast<int>(button);
    return b >= 0 && b < kGamepadButtonCount ? kLabels[b] : "";
}

const char* PadAxisLabel(GamepadAxis axis) {
    constexpr const char* kLabels[kGamepadAxisCount] = {"Analógico esq. ↔", "Analógico esq. ↕", "Analógico dir. ↔",
                                                        "Analógico dir. ↕", "Gatilho esq. (LT)", "Gatilho dir. (RT)"};
    const int a = static_cast<int>(axis);
    return a >= 0 && a < kGamepadAxisCount ? kLabels[a] : "";
}

std::string BindingLabel(const InputBinding& b) {
    switch (b.source) {
        case InputBinding::Source::Key: return KeyLabel(static_cast<Key>(b.code));
        case InputBinding::Source::Mouse: return MouseLabel(static_cast<MouseButton>(b.code));
        case InputBinding::Source::GamepadButton: return PadButtonLabel(static_cast<GamepadButton>(b.code));
        case InputBinding::Source::GamepadAxis: return PadAxisLabel(static_cast<GamepadAxis>(b.code));
        case InputBinding::Source::MouseAxis: return MouseAxisLabel(static_cast<MouseAxis>(b.code));
    }
    return {};
}

bool FromGamepad(const InputBinding& b) {
    return b.source == InputBinding::Source::GamepadButton || b.source == InputBinding::Source::GamepadAxis;
}

// Analógico e mouse têm sensibilidade; teclas e botões só valem 0 ou 1 (a escala deles é só o sentido).
bool HasSensitivity(const InputBinding& b) {
    return b.source == InputBinding::Source::GamepadAxis || b.source == InputBinding::Source::MouseAxis ||
           std::abs(std::abs(b.scale) - 1.0f) > 1e-4f;
}

// --- Escolha de entradas e contextos ------------------------------------------------------------------------

// Menu "Escolher": todas as entradas por tipo (sem controle conectado, é o jeito de ligar um botão dele).
std::optional<InputBinding> ChooseBindingMenu() {
    std::optional<InputBinding> chosen;
    if (ImGui::BeginMenu("Teclado")) {
        for (int k = static_cast<int>(kFirstKey); k <= static_cast<int>(kLastKey); ++k) {
            const Key key = static_cast<Key>(k);
            if (*KeyName(key) && ImGui::MenuItem(KeyLabel(key).c_str())) chosen = InputBinding::Of(key);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Mouse")) {
        for (int b = 0; b < kMouseButtonCount; ++b) {
            if (ImGui::MenuItem(MouseLabel(static_cast<MouseButton>(b)))) chosen = InputBinding::Of(static_cast<MouseButton>(b));
        }
        ImGui::Separator();
        for (int a = 0; a < kMouseAxisCount; ++a) {
            if (ImGui::MenuItem(MouseAxisLabel(static_cast<MouseAxis>(a)))) chosen = InputBinding::Of(static_cast<MouseAxis>(a));
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Controle")) {
        for (int b = 0; b < kGamepadButtonCount; ++b) {
            if (ImGui::MenuItem(PadButtonLabel(static_cast<GamepadButton>(b)))) {
                chosen = InputBinding::Of(static_cast<GamepadButton>(b));
            }
        }
        ImGui::Separator();
        for (int a = 0; a < kGamepadAxisCount; ++a) {
            if (ImGui::MenuItem(PadAxisLabel(static_cast<GamepadAxis>(a)))) chosen = InputBinding::Of(static_cast<GamepadAxis>(a));
        }
        ImGui::EndMenu();
    }
    return chosen;
}

// Os contextos usados no mapa, na ordem em que aparecem (o inicial primeiro), sem repetir.
std::vector<std::string> ContextsOf(const InputActions& actions) {
    std::vector<std::string> out;
    const auto add = [&](const std::string& c) {
        if (c.empty()) return;
        if (std::none_of(out.begin(), out.end(), [&](const std::string& o) { return SameText(o, c); })) out.push_back(c);
    };
    add(actions.startContext);
    for (const InputAction& a : actions.actions) add(a.context);
    return out;
}

// Lista dos contextos que existem, "Sempre" (sem contexto) e um campo para criar um novo: escrever à mão deixava
// "Meun" no lugar de "Menu" passar sem aviso.
bool ContextCombo(const char* id, std::string& value, const std::vector<std::string>& contexts, float width) {
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, value.empty() ? "Sempre (sem contexto)" : value.c_str())) {
        for (const std::string& c : contexts) {
            if (ImGui::Selectable(c.c_str(), SameText(c, value)) && c != value) {
                value = c;
                changed = true;
            }
        }
        if (ImGui::Selectable("Sempre (sem contexto)", value.empty()) && !value.empty()) {
            value.clear();
            changed = true;
        }
        ImGui::Separator();
        static std::string fresh;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##novo", "novo contexto + Enter", &fresh, ImGuiInputTextFlags_EnterReturnsTrue) &&
            !fresh.empty()) {
            value = std::exchange(fresh, {});
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndCombo();
    }
    return changed;
}

// Rótulo antes do campo (o padrão do ImGui é depois, e "Mover  Nome" lia como uma coisa só).
void FieldLabel(const char* label, float width) {
    const float x = ImGui::GetCursorPosX();  // já com o recuo da árvore (SameLine(x) não o conta)
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x + width));
}

// --- Avisos -------------------------------------------------------------------------------------------------

std::vector<std::string> ActionProblems(const InputAction& action) {
    std::vector<std::string> out;
    if (action.bindings.empty()) out.push_back("Sem nenhuma entrada: a ação nunca vale nada.");
    if (action.type == InputActionType::Vector2 && !action.bindings.empty() &&
        std::none_of(action.bindings.begin(), action.bindings.end(),
                     [](const InputBinding& b) { return b.part == InputBinding::Part::Y; })) {
        out.push_back("Vetor 2D sem nada no Y: o Y fica sempre 0 (ponha W/S ou o analógico ↕ no lado Y).");
    }
    if (action.type == InputActionType::Button &&
        std::any_of(action.bindings.begin(), action.bindings.end(), [](const InputBinding& b) { return b.scale < 0.0f; })) {
        out.push_back("Botão com entrada invertida: um botão vai de 0 a 1, o sentido negativo não conta.");
    }
    return out;
}

void WarningMark(const std::string& text) {
    ImGui::TextColored(kWarnColor, "!");
    ImGui::SetItemTooltip("%s", text.c_str());
}

std::string Joined(const std::vector<std::string>& items, std::string_view separator) {
    std::string out;
    for (const std::string& s : items) out += (out.empty() ? "" : std::string(separator)) + s;
    return out;
}

// Valor ao vivo: barra para botão e eixo, círculo com um ponto para o vetor 2D.
void ValuePreview(const InputAction& action, float width) {
    const glm::vec2 v = InputActions::VectorOf(action);
    const float h = ImGui::GetFrameHeight() * 0.7f;
    if (action.type == InputActionType::Vector2) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float r = h * 0.5f;
        const ImVec2 c(p.x + width * 0.5f, p.y + ImGui::GetFrameHeight() * 0.5f);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddCircle(c, r, ImGui::GetColorU32(ImGuiCol_TextDisabled), 0, 1.0f);
        const float len = std::sqrt(v.x * v.x + v.y * v.y);
        const glm::vec2 d = len > 1.0f ? v / len : v;  // o mouse rápido passa de 1: o ponto fica na borda
        draw->AddCircleFilled(ImVec2(c.x + d.x * r, c.y - d.y * r), 2.5f,
                              ImGui::GetColorU32(len > 0.0f ? ImGuiCol_PlotHistogram : ImGuiCol_TextDisabled));
        ImGui::Dummy(ImVec2(width, ImGui::GetFrameHeight()));
    } else {
        const float fill = action.type == InputActionType::Button ? v.x : (v.x + 1.0f) * 0.5f;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ImGui::GetFrameHeight() - h) * 0.5f);
        ImGui::ProgressBar(std::clamp(fill, 0.0f, 1.0f), ImVec2(width, h), "");
    }
    ImGui::SetItemTooltip("Agora: %+.2f%s", v.x,
                          action.type == InputActionType::Vector2 ? std::format(", {:+.2f}", v.y).c_str() : "");
}

// --- Tabela de entradas -------------------------------------------------------------------------------------

struct WithOption {
    int key;
    const char* label;
};
constexpr WithOption kWithOptions[] = {{0, "—"},
                                       {static_cast<int>(Key::LeftControl), "Ctrl +"},
                                       {static_cast<int>(Key::LeftShift), "Shift +"},
                                       {static_cast<int>(Key::LeftAlt), "Alt +"}};

// Uma linha da tabela. Devolve se mudou; `remove` fica true no ×.
bool BindingRow(InputBinding& b, const InputAction& action, const InputActions& all, bool& remove) {
    bool changed = false;
    ImGui::TableNextRow();
    // Entrada (com o aviso de tecla repetida).
    ImGui::TableNextColumn();
    const std::vector<std::string> conflicts = all.Conflicts(action.name, b);
    if (!conflicts.empty()) {
        WarningMark(std::format("Também em {}: as duas valem ao mesmo tempo (mesmo contexto).", Joined(conflicts, ", ")));
        ImGui::SameLine();
    }
    const std::string with = b.with != 0 ? KeyLabel(static_cast<Key>(b.with)) + " + " : std::string();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted((with + BindingLabel(b)).c_str());
    // Lado (só num vetor 2D).
    if (action.type == InputActionType::Vector2) {
        ImGui::TableNextColumn();
        int part = b.part == InputBinding::Part::Y ? 1 : 0;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##lado", &part, "X\0Y\0")) {
            b.part = part == 1 ? InputBinding::Part::Y : InputBinding::Part::X;
            changed = true;
        }
        ImGui::SetItemTooltip("Lado do vetor: em Mover, A e D vão no X, W e S no Y.");
    }
    // Inverter.
    ImGui::TableNextColumn();
    bool inverted = b.scale < 0.0f;
    if (ImGui::Checkbox("##inv", &inverted)) {
        b.scale = -b.scale;
        changed = true;
    }
    ImGui::SetItemTooltip("Empurra para o lado negativo (A em Horizontal, S em Mover).");
    // Sensibilidade (analógico e mouse).
    ImGui::TableNextColumn();
    if (HasSensitivity(b)) {
        float amount = std::abs(b.scale);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragFloat("##sens", &amount, 0.01f, 0.05f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
            b.scale = inverted ? -amount : amount;
            changed = true;
        }
        ImGui::SetItemTooltip(b.source == InputBinding::Source::MouseAxis
                                  ? "Sensibilidade: com 1, o mouse a 1000 px/s vale o analógico encostado."
                                  : "Sensibilidade: multiplica o valor do eixo.");
    }
    // Tecla de junto.
    ImGui::TableNextColumn();
    const auto current = std::find_if(std::begin(kWithOptions), std::end(kWithOptions),
                                      [&](const WithOption& o) { return o.key == b.with; });
    const std::string preview = current != std::end(kWithOptions) ? current->label : KeyLabel(static_cast<Key>(b.with));
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##junto", preview.c_str())) {
        for (const WithOption& o : kWithOptions) {
            if (ImGui::Selectable(o.label, o.key == b.with)) {
                b.with = o.key;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Tecla apertada junto (Ctrl+S). Vale o lado esquerdo ou o direito.");
    // Remover.
    ImGui::TableNextColumn();
    if (ImGui::SmallButton("×")) remove = true;
    ImGui::SetItemTooltip("Remover esta entrada");
    return changed;
}

// As entradas da ação: teclado e mouse primeiro, depois o controle. Devolve se mudou.
bool BindingsTable(InputAction& action, const InputActions& all) {
    std::vector<int> order(action.bindings.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::stable_partition(order.begin(), order.end(), [&](int i) { return !FromGamepad(action.bindings[static_cast<size_t>(i)]); });

    const bool vector = action.type == InputActionType::Vector2;
    if (!ImGui::BeginTable("entradas", vector ? 6 : 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                          ImGuiTableFlags_SizingFixedFit)) {
        return false;
    }
    ImGui::TableSetupColumn("Entrada", ImGuiTableColumnFlags_WidthStretch);
    if (vector) ImGui::TableSetupColumn("Lado", ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableSetupColumn("Inverter", ImGuiTableColumnFlags_WidthFixed, 56.0f);
    ImGui::TableSetupColumn("Sensib.", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Junto", ImGuiTableColumnFlags_WidthFixed, 78.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 22.0f);
    ImGui::TableHeadersRow();

    bool changed = false;
    int remove = -1;
    bool gamepadTitle = false;
    for (int i : order) {
        InputBinding& b = action.bindings[static_cast<size_t>(i)];
        if (FromGamepad(b) && !gamepadTitle && i != order.front()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("Controle");
        }
        gamepadTitle = gamepadTitle || FromGamepad(b);
        ImGui::PushID(i);
        bool removeThis = false;
        if (BindingRow(b, action, all, removeThis)) changed = true;
        if (removeThis) remove = i;
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (remove >= 0) {
        action.bindings.erase(action.bindings.begin() + remove);
        changed = true;
    }
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
    ImGui::SetNextWindowSize(ImVec2(560.0f, 600.0f), ImGuiCond_FirstUseEver);
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
    const std::vector<std::string> contexts = ContextsOf(actions);
    const float labelWidth = ImGui::CalcTextSize("Contexto inicial").x + ImGui::GetStyle().ItemSpacing.x * 2.0f;

    // Cabeçalho: ajuda, controles ligados e o contexto inicial.
    ImGui::TextDisabled("(?)");
    ImGui::SetItemTooltip("Os scripts perguntam pela ação, não pela tecla:\n"
                          "  WasActionPressed(\"Pular\"), GetVector(\"Mover\"), GetAxis(\"Horizontal\").\n"
                          "Só valem as ações do contexto ativo (SetInputContext(\"Menu\")) e as sem contexto.\n"
                          "As mudanças valem a partir do próximo Play.");
    ImGui::SameLine();
    if (Input::IsGamepadConnected()) {
        for (int pad = 0; pad < kMaxGamepads; ++pad) {
            if (!Input::IsGamepadConnected(pad)) continue;
            const bool inUse = Input::LastDevice() == InputDevice::Gamepad && Input::LastGamepad() == pad;
            ImGui::TextColored(kOkColor, "Controle %d: %s%s", pad + 1, Input::GamepadName(pad).c_str(),
                               inUse ? " (em uso)" : "");
        }
    } else {
        ImGui::TextDisabled("Nenhum controle conectado (os botões dele estão em \"Escolher\").");
    }
    FieldLabel("Contexto inicial", labelWidth);
    if (ContextCombo("##inicial", actions.startContext, contexts, 160.0f)) changed = true;
    ImGui::SetItemTooltip("O contexto ativo quando o jogo começa.");
    ImGui::Separator();

    // Uma seção por contexto; as sem contexto por último.
    std::vector<std::string> sections = contexts;
    sections.emplace_back();
    int removeAction = -1;
    for (const std::string& section : sections) {
        std::vector<int> members;
        for (int i = 0; i < static_cast<int>(actions.actions.size()); ++i) {
            const std::string& c = actions.actions[static_cast<size_t>(i)].context;
            if (section.empty() ? c.empty() : SameText(c, section)) members.push_back(i);
        }
        if (members.empty() && section.empty()) continue;
        const bool start = !section.empty() && SameText(section, actions.startContext);
        const std::string title = std::format("{}{} ({})###secao{}", section.empty() ? "Sempre (sem contexto)" : section,
                                              start ? " · inicial" : "", members.size(), section);
        if (!ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) continue;

        for (int i : members) {
            InputAction& action = actions.actions[static_cast<size_t>(i)];
            ImGui::PushID(i);
            const std::vector<std::string> problems = ActionProblems(action);

            // Cabeçalho da ação numa tabela (nome e tipo | valor ao vivo | Excluir): a tabela cabe na largura
            // da janela sozinha, sem posição calculada à mão que passava da borda.
            bool open = false;
            const float previewWidth = 60.0f;
            const float deleteWidth = ImGui::CalcTextSize("Excluir").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            if (ImGui::BeginTable("cabecalho", 3, ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("acao", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("valor", ImGuiTableColumnFlags_WidthFixed, previewWidth);
                ImGui::TableSetupColumn("excluir", ImGuiTableColumnFlags_WidthFixed, deleteWidth);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                // Sem TreePush: o conteúdo aberto vem depois da tabela, com o recuo posto à mão.
                open = ImGui::TreeNodeEx("##acao", ImGuiTreeNodeFlags_NoTreePushOnOpen, "%s", action.name.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%s", kTypeLabels[static_cast<int>(action.type)]);
                if (!problems.empty()) {
                    ImGui::SameLine();
                    WarningMark(Joined(problems, "\n"));
                }
                ImGui::TableNextColumn();
                ValuePreview(action, previewWidth);
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Excluir")) ImGui::OpenPopup("excluir");
                if (ImGui::BeginPopupModal("Excluir a ação?###excluir", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Excluir \"%s\" e as %zu entradas dela?", action.name.c_str(), action.bindings.size());
                    ImGui::TextDisabled("Os scripts que perguntam por ela passam a receber 0 (e um aviso no log).");
                    if (ImGui::Button("Excluir")) {
                        removeAction = i;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancelar")) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                ImGui::EndTable();
            }

            if (open) {
                ImGui::Indent();
                const float fieldWidth = std::min(260.0f, ImGui::GetContentRegionAvail().x - labelWidth);
                std::string name = action.name;
                FieldLabel("Nome", labelWidth);
                ImGui::SetNextItemWidth(fieldWidth);
                ImGui::InputText("##nome", &name);
                if (ImGui::IsItemDeactivatedAfterEdit() && !name.empty() && name != action.name) {
                    const InputAction* other = actions.Find(name);
                    if (!other || other == &action) {
                        action.name = name;
                        changed = true;
                    }
                }
                int type = static_cast<int>(action.type);
                FieldLabel("Tipo", labelWidth);
                ImGui::SetNextItemWidth(fieldWidth);
                if (ImGui::Combo("##tipo", &type, kTypeLabels, IM_ARRAYSIZE(kTypeLabels))) {
                    action.type = static_cast<InputActionType>(type);
                    changed = true;
                }
                ImGui::SetItemTooltip("Botão: 0 a 1 (Pular). Eixo: -1 a 1, GetAxis (Horizontal). Vetor 2D: X e Y, "
                                      "GetVector, com a diagonal limitada (Mover, Olhar).");
                FieldLabel("Contexto", labelWidth);
                if (ContextCombo("##contexto", action.context, contexts, fieldWidth)) changed = true;
                ImGui::SetItemTooltip("Só vale com este contexto ativo. \"Sempre\": vale em qualquer um.");

                if (BindingsTable(action, actions)) changed = true;

                if (m_listeningAction == i) {
                    const double left = kListenSeconds - (ImGui::GetTime() - m_listeningSince);
                    ImGui::TextColored(kWarnColor, "Aperte uma tecla ou um botão do controle... (%.0f s)", std::ceil(left));
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Cancelar") || left <= 0.0) m_listeningAction = -1;
                    // Sem o mouse: o clique em "Cancelar" não pode virar a entrada (o mouse está em "Escolher").
                    if (const auto pressed = InputActions::FirstPressed(false); pressed && m_listeningAction == i) {
                        if (std::find(action.bindings.begin(), action.bindings.end(), *pressed) == action.bindings.end()) {
                            action.bindings.push_back(*pressed);
                            changed = true;
                        }
                        m_listeningAction = -1;
                    }
                } else {
                    if (ImGui::SmallButton("+ Apertar")) {
                        m_listeningAction = i;
                        m_listeningSince = ImGui::GetTime();
                    }
                    ImGui::SetItemTooltip("Liga a próxima tecla ou botão do controle apertado.");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("+ Escolher...")) ImGui::OpenPopup("escolher");
                    ImGui::SetItemTooltip("Escolhe da lista: teclado, mouse (botões e movimento) e controle.");
                    if (ImGui::BeginPopup("escolher")) {
                        if (const auto chosen = ChooseBindingMenu()) {
                            action.bindings.push_back(*chosen);
                            changed = true;
                        }
                        ImGui::EndPopup();
                    }
                }
                ImGui::Spacing();
                ImGui::Unindent();
            }
            ImGui::PopID();
        }
        ImGui::PushID(section.c_str());
        if (ImGui::SmallButton("+ Nova ação")) {
            std::string name = "Nova ação";
            for (int n = 2; actions.Find(name); ++n) name = std::format("Nova ação {}", n);
            InputAction fresh{name, {}, InputActionType::Button, section};
            actions.actions.push_back(std::move(fresh));
            changed = true;
        }
        ImGui::PopID();
        ImGui::Spacing();
    }
    if (removeAction >= 0) {
        actions.actions.erase(actions.actions.begin() + removeAction);
        m_listeningAction = -1;
        changed = true;
    }

    ImGui::Separator();
    if (ImGui::Button("Restaurar o padrão")) ImGui::OpenPopup("restaurar");
    ImGui::SetItemTooltip("Jogo: Mover, Olhar, Horizontal, Vertical, Olhar X/Y, Pular, Correr, Interagir e Pausa. "
                          "Menu: Navegar, Confirmar e Voltar. Com teclado, mouse e controle.");
    if (ImGui::BeginPopupModal("Restaurar o padrão?###restaurar", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Trocar o mapa inteiro pelo padrão? As ações e teclas deste projeto se perdem.");
        if (ImGui::Button("Restaurar")) {
            actions = InputActions::Defaults();
            m_listeningAction = -1;
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (changed && !(actions == m_project->Actions())) {
        std::string error;
        if (!SetInputActions(actions, error)) ShowError("Não foi possível salvar os controles: " + error);
    }
    ImGui::End();
}

} // namespace editor
