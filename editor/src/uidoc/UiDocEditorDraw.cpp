// Editor de UI: o desenho da janela com o ImGui (barra, paleta, árvore, prévia com as alças, propriedades, estilos e o
// popup de salvar). O estado e as operações ficam em UiDocEditor.cpp.
#include "uidoc/UiDocEditor.h"

#include "drawers/FieldDrawers.h"

#include <motor/assets/AssetDatabase.h>
#include <motor/assets/AssetLibrary.h>
#include <motor/assets/AssetLoaders.h>
#include <motor/core/Paths.h>
#include <motor/renderer/Framebuffer.h>
#include <motor/ui/UiDocument.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <format>

namespace editor {

using namespace motor;
namespace fs = std::filesystem;

namespace {

constexpr const char* kElementPayload = "MOTOR_UI_ELEM";  // caminho de um elemento da árvore
constexpr const char* kNewPayload = "MOTOR_UI_NEW";       // tipo de um elemento da paleta
constexpr const char* kSaveTitle = "Salvar documento de UI?";
constexpr ImU32 kOutlineColor = IM_COL32(255, 255, 255, 60);
constexpr ImU32 kHoverColor = IM_COL32(120, 190, 255, 200);
constexpr ImU32 kSelectedColor = IM_COL32(255, 158, 38, 255);  // a mesma da seleção no Viewport
constexpr ImU32 kDropColor = IM_COL32(120, 190, 255, 255);
constexpr float kHandleSize = 8.0f;
constexpr int kZoomChoices[] = {0, 25, 50, 75, 100, 150};

struct PathPayload {
    int depth = 0;
    std::array<int, 32> index{};
};

PathPayload ToPayload(const UiPath& path) {
    PathPayload p;
    p.depth = static_cast<int>(std::min(path.size(), p.index.size()));
    for (int i = 0; i < p.depth; ++i) p.index[static_cast<size_t>(i)] = path[static_cast<size_t>(i)];
    return p;
}

UiPath FromPayload(const void* data) {
    PathPayload p;
    std::memcpy(&p, data, sizeof(p));
    return UiPath(p.index.begin(), p.index.begin() + std::clamp(p.depth, 0, static_cast<int>(p.index.size())));
}

const char* TypeLabel(UiElementType type) {
    switch (type) {
        case UiElementType::Panel: return "Painel";
        case UiElementType::Text: return "Texto";
        case UiElementType::Image: return "Imagem";
        case UiElementType::Button: return "Botão";
    }
    return "?";
}

constexpr UiElementType kTypes[] = {UiElementType::Panel, UiElementType::Text, UiElementType::Image,
                                    UiElementType::Button};

UiPath ParentOf(const UiPath& path) { return path.empty() ? path : UiPath(path.begin(), path.end() - 1); }

// Onde entra um elemento novo solto em `at`: dentro de um painel (no fim), depois de qualquer outro; nada = raiz.
std::pair<UiPath, int> InsertTarget(const std::vector<UiElement>& roots, const UiPath& at) {
    const UiElement* e = UiElementAt(roots, at);
    if (!e) return {{}, -1};
    if (e->type == UiElementType::Panel) return {at, -1};
    return {ParentOf(at), at.back() + 1};
}

struct PropertyGroup {
    const char* title;
    std::vector<const char*> names;
};

const std::vector<PropertyGroup>& PropertyGroups() {
    static const std::vector<PropertyGroup> kGroups = {
        {"Lugar e tamanho", {"placement", "anchor", "position", "width", "height", "margin", "grow", "shrink", "alignSelf"}},
        {"Filhos", {"direction", "justify", "alignItems", "gap", "padding", "overflow"}},
        {"Cor e texto",
         {"color", "font", "fontSize", "textColor", "align", "verticalAlign", "wrap", "normalColor", "hoverColor",
          "pressedColor"}},
        {"Visual",
         {"radius", "borderWidth", "borderColor", "slice", "opacity", "textOutlineWidth", "textOutlineColor",
          "textShadowOffset", "textShadowColor", "scale", "transition"}},
    };
    return kGroups;
}

// Editor de um valor de estilo, pela forma do valor (a mesma tabela do .mui e do .mstyle). true se mudou.
bool EditStyleValue(EditorContext& ctx, int property, UiStyleValue& value) {
    bool changed = false;
    std::visit(
        [&](auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>) {
                changed = ImGui::Checkbox("##v", &v);
            } else if constexpr (std::is_same_v<T, float>) {
                changed = ImGui::DragFloat("##v", &v, 0.5f, 0.0f, 0.0f, "%.2f");
            } else if constexpr (std::is_same_v<T, glm::vec2>) {
                changed = ImGui::DragFloat2("##v", &v.x, 0.5f, 0.0f, 0.0f, "%.1f");
            } else if constexpr (std::is_same_v<T, glm::vec4>) {
                changed = ImGui::ColorEdit4("##v", &v.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
            } else if constexpr (std::is_same_v<T, UiLength>) {
                const char* units[] = {"auto", "px", "%"};
                const float w = ImGui::GetContentRegionAvail().x;
                ImGui::SetNextItemWidth(std::max(40.0f, w * 0.35f));
                int unit = static_cast<int>(v.unit);
                if (ImGui::Combo("##u", &unit, units, 3)) {
                    v.unit = static_cast<UiUnit>(unit);
                    if (v.unit == UiUnit::Percent && v.value > 100.0f) v.value = 100.0f;
                    changed = true;
                }
                if (v.unit != UiUnit::Auto) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    changed |= ImGui::DragFloat("##n", &v.value, 1.0f, 0.0f, 100000.0f, "%.0f");
                }
            } else if constexpr (std::is_same_v<T, UiEdges>) {
                float e[4] = {v.top, v.right, v.bottom, v.left};
                if (ImGui::DragFloat4("##v", e, 0.5f, 0.0f, 10000.0f, "%.0f")) {
                    v = {e[0], e[1], e[2], e[3]};
                    changed = true;
                }
                ImGui::SetItemTooltip("cima, direita, baixo, esquerda");
            } else if constexpr (std::is_same_v<T, int>) {
                const auto names = UiStylePropertyEnumNames(property);
                const std::string current = v >= 0 && v < static_cast<int>(names.size())
                                                ? std::string(names[static_cast<size_t>(v)])
                                                : std::to_string(v);
                if (ImGui::BeginCombo("##v", current.c_str())) {
                    for (size_t i = 0; i < names.size(); ++i) {
                        const std::string label(names[i]);
                        if (ImGui::Selectable(label.c_str(), static_cast<int>(i) == v)) {
                            v = static_cast<int>(i);
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            } else if constexpr (std::is_same_v<T, UiFont>) {
                const std::string current = v.path.empty() ? v.guid : v.path;
                if (const auto chosen = AssetCombo(ctx, "##v", AssetType::Font, v.IsDefault() ? nullptr : &current,
                                                   "(padrão: Roboto)")) {
                    v = {};
                    if (!chosen->empty()) {
                        v.guid = ctx.assets.Database().GuidFor(ctx.assets.FileForKey(*chosen));
                        v.path = *chosen;
                    }
                    changed = true;
                }
            }
        },
        value);
    return changed;
}

void DrawRect(ImDrawList* dl, const ImVec2& origin, float zoom, const UiBox& box, ImU32 color, float thickness) {
    dl->AddRect(ImVec2(origin.x + box.min.x * zoom, origin.y + box.min.y * zoom),
                ImVec2(origin.x + box.max.x * zoom, origin.y + box.max.y * zoom), color, 0.0f, 0, thickness);
}

// As 8 alças de uma caixa (na tela): o ponto e os lados que ela move.
struct HandleSpot {
    ImVec2 at;
    int sides;
};
std::array<HandleSpot, 8> HandlesOf(const ImVec2& a, const ImVec2& b) {
    using H = UiDocEditor::Handle;
    const float mx = (a.x + b.x) * 0.5f, my = (a.y + b.y) * 0.5f;
    return {{{a, H::kLeft | H::kTop},
             {ImVec2(mx, a.y), H::kTop},
             {ImVec2(b.x, a.y), H::kRight | H::kTop},
             {ImVec2(b.x, my), H::kRight},
             {b, H::kRight | H::kBottom},
             {ImVec2(mx, b.y), H::kBottom},
             {ImVec2(a.x, b.y), H::kLeft | H::kBottom},
             {ImVec2(a.x, my), H::kLeft}}};
}

ImGuiMouseCursor CursorFor(int sides) {
    using H = UiDocEditor::Handle;
    const bool h = sides & (H::kLeft | H::kRight), v = sides & (H::kTop | H::kBottom);
    if (h && v) {
        const bool main = (sides & H::kLeft && sides & H::kTop) || (sides & H::kRight && sides & H::kBottom);
        return main ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW;
    }
    return h ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS;
}

} // namespace

void UiDocEditor::Draw() {
    m_focused = false;
    if (IsOpen() && std::chrono::steady_clock::now() >= m_nextFileCheck) CheckFilesOnDisk();
    if (m_visible) {
        const std::string name = IsOpen() ? ToUtf8(m_file.filename()) : std::string("(nenhum documento)");
        const std::string title = std::format("Editor de UI — {}{}###EditorDeUI", name, HasUnsavedChanges() ? "*" : "");
        ImGui::SetNextWindowSize(ImVec2(1100.0f, 640.0f), ImGuiCond_FirstUseEver);
        if (std::exchange(m_focusRequest, false)) ImGui::SetNextWindowFocus();
        bool open = true;
        if (ImGui::Begin(title.c_str(), &open)) {
            m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            DrawToolbar();
            if (IsOpen()) {
                const float left = 250.0f, right = 360.0f;
                ImGui::BeginChild("esquerda", ImVec2(left, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
                DrawPalette();
                ImGui::SeparatorText("Árvore");
                DrawTree();
                ImGui::EndChild();
                ImGui::SameLine();
                const float centerWidth = std::max(100.0f, ImGui::GetContentRegionAvail().x - right);
                ImGui::BeginChild("previa", ImVec2(centerWidth, 0.0f), ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_HorizontalScrollbar);
                DrawPreview();
                ImGui::EndChild();
                ImGui::SameLine();
                ImGui::BeginChild("direita", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
                if (ImGui::BeginTabBar("abas")) {
                    if (ImGui::BeginTabItem("Propriedades")) {
                        DrawProperties();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Estilos")) {
                        DrawStyles();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                ImGui::EndChild();
            }
            if (m_focused) HandleShortcuts();
        }
        ImGui::End();
        if (!open) {
            if (IsOpen()) {
                AskToSave(Pending::Close);
            } else {
                m_visible = false;
            }
        }
    }
    for (auto& fn : std::exchange(m_deferred, {})) fn();
    DrawSavePopup();
    // Fim de uma interação (soltou o mouse, saiu do campo): o que mudou vira um passo de desfazer.
    if (m_resizeHandles == 0 && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) Commit();
}

void UiDocEditor::DrawToolbar() {
    if (!m_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", m_error.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("OK")) m_error.clear();
    }
    if (!IsOpen()) {
        ImGui::TextUnformatted("Abra um documento (.mui): duplo clique no painel Projeto ou escolha aqui:");
        ImGui::SetNextItemWidth(320.0f);
        if (const auto chosen = AssetCombo(m_host.ctx, "##abrir", AssetType::UiDocument, nullptr, nullptr)) {
            if (!chosen->empty()) RequestOpen(m_host.ctx.assets.FileForKey(*chosen));
        }
        return;
    }
    ImGui::BeginDisabled(!HasUnsavedChanges());
    if (ImGui::Button("Salvar")) {
        std::string error;
        if (!Save(&error)) m_error = "Não foi possível gravar: " + error;
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Grava o .mui e as folhas .mstyle mudadas (Ctrl+S)");
    ImGui::SameLine();
    ImGui::BeginDisabled(!CanUndo());
    if (ImGui::Button("Desfazer")) Undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!CanRedo());
    if (ImGui::Button("Refazer")) Redo();
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    const auto& list = Resolutions();
    std::string current = std::format("Personalizada ({}x{})", m_resolution.x, m_resolution.y);
    for (const Resolution& r : list) {
        if (r.width == m_resolution.x && r.height == m_resolution.y) current = r.label;
    }
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("##resolucao", current.c_str())) {
        for (const Resolution& r : list) {
            if (ImGui::Selectable(r.label, r.width == m_resolution.x && r.height == m_resolution.y)) {
                SetResolution(r.width, r.height);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Resolução da prévia (a UI escala pela altura, como no jogo)");
    ImGui::SameLine();
    int size[2] = {m_resolution.x, m_resolution.y};
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt2("##tamanho", size, ImGuiInputTextFlags_EnterReturnsTrue)) SetResolution(size[0], size[1]);
    ImGui::SetItemTooltip("Resolução personalizada (Enter aplica)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    const std::string zoomLabel = m_zoomChoice == 0 ? std::string("Ajustar") : std::format("{}%", m_zoomChoice);
    if (ImGui::BeginCombo("##zoom", zoomLabel.c_str())) {
        for (const int z : kZoomChoices) {
            const std::string label = z == 0 ? std::string("Ajustar") : std::format("{}%", z);
            if (ImGui::Selectable(label.c_str(), z == m_zoomChoice)) m_zoomChoice = z;
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Tamanho da prévia na janela");
    ImGui::SameLine();
    ImGui::TextDisabled("escala da UI %.2f", UiScale(glm::vec2(m_resolution)));
    ImGui::SetItemTooltip("Pixels da tela por pixel de referência (altura / %.0f), como no jogo", kUiReferenceHeight);

    if (m_changedOnDisk) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "O arquivo mudou fora do editor.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reler do disco (descarta as alterações)")) {
            const fs::path file = m_file;
            std::string error;
            if (!Open(file, &error)) m_error = "Não abriu: " + error;
        }
    }
}

void UiDocEditor::DrawPalette() {
    ImGui::SeparatorText("Paleta");
    const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    for (int i = 0; i < 4; ++i) {
        const UiElementType type = kTypes[i];
        if (i % 2) ImGui::SameLine();
        if (ImGui::Button(TypeLabel(type), ImVec2(w, 0.0f))) {
            const auto [parent, index] = InsertTarget(m_state.doc.elements, m_state.selected);
            AddElement(type, parent, index);
        }
        ImGui::SetItemTooltip("Clique: põe no selecionado (dentro de um painel). Arraste para a árvore ou a prévia.");
        if (ImGui::BeginDragDropSource()) {
            const int t = static_cast<int>(type);
            ImGui::SetDragDropPayload(kNewPayload, &t, sizeof(t));
            ImGui::Text("Novo: %s", TypeLabel(type));
            ImGui::EndDragDropSource();
        }
    }
}

void UiDocEditor::DrawTree() {
    UiPath path;
    for (size_t i = 0; i < m_state.doc.elements.size(); ++i) {
        path = {static_cast<int>(i)};
        ImGui::PushID(static_cast<int>(i));
        DrawTreeNode(m_state.doc.elements[i], path);
        ImGui::PopID();
    }
    // O espaço que sobra embaixo: soltar aqui põe no fim da raiz.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##raiz", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 24.0f)));
    if (ImGui::IsItemClicked()) Select({});
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kElementPayload)) {
            const UiPath from = FromPayload(p->Data);
            m_deferred.push_back([this, from] { MoveElement(from, {}, -1); });
        }
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kNewPayload)) {
            const auto type = static_cast<UiElementType>(*static_cast<const int*>(p->Data));
            m_deferred.push_back([this, type] { AddElement(type, {}, -1); });
        }
        ImGui::EndDragDropTarget();
    }
}

void UiDocEditor::DrawTreeNode(UiElement& element, UiPath& path) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (element.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (path == m_state.selected) flags |= ImGuiTreeNodeFlags_Selected;
    std::string label = std::format("{} {}", TypeLabel(element.type), element.name.empty() ? "(sem nome)" : element.name);
    for (const std::string& c : element.classes) label += " ." + c;
    if (!element.visible) label += " (oculto)";
    const bool open = ImGui::TreeNodeEx("##no", flags, "%s", label.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) Select(path);
    if (ImGui::BeginDragDropSource()) {
        const PathPayload payload = ToPayload(path);
        ImGui::SetDragDropPayload(kElementPayload, &payload, sizeof(payload));
        ImGui::TextUnformatted(label.c_str());
        ImGui::EndDragDropSource();
    }
    DrawTreeDropTarget(path, open && !element.children.empty());
    if (ImGui::BeginPopupContextItem("##menu")) {
        const UiPath at = path;
        Select(at);
        if (ImGui::BeginMenu("Adicionar dentro")) {
            for (const UiElementType type : kTypes) {
                if (ImGui::MenuItem(TypeLabel(type))) m_deferred.push_back([this, type, at] { AddElement(type, at, -1); });
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Duplicar", "Ctrl+D")) m_deferred.push_back([this, at] { DuplicateElement(at); });
        if (ImGui::MenuItem("Excluir", "Del")) m_deferred.push_back([this, at] { DeleteElement(at); });
        ImGui::EndPopup();
    }
    if (open) {
        for (size_t i = 0; i < element.children.size(); ++i) {
            path.push_back(static_cast<int>(i));
            ImGui::PushID(static_cast<int>(i));
            DrawTreeNode(element.children[i], path);
            ImGui::PopID();
            path.pop_back();
        }
        ImGui::TreePop();
    }
}

void UiDocEditor::DrawTreeDropTarget(const UiPath& path, bool isOpenWithChildren) {
    if (!ImGui::BeginDragDropTarget()) return;
    // Faixa de cima: antes do elemento; de baixo: depois (se não estiver aberto com filhos); meio: dentro dele.
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    const float y = (ImGui::GetMousePos().y - min.y) / std::max(1.0f, max.y - min.y);
    UiPath parent = path;
    int index = -1;
    int zone = 0;  // -1 antes, 0 dentro, +1 depois
    if (y < 0.25f) {
        zone = -1;
        parent = ParentOf(path);
        index = path.back();
    } else if (y > 0.75f && !isOpenWithChildren) {
        zone = 1;
        parent = ParentOf(path);
        index = path.back() + 1;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiDragDropFlags flags = ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
    auto indicate = [&] {
        if (zone == 0) {
            dl->AddRect(min, max, kDropColor, 2.0f, 0, 2.0f);
        } else {
            const float ly = zone < 0 ? min.y : max.y;
            dl->AddLine(ImVec2(min.x, ly), ImVec2(max.x, ly), kDropColor, 2.0f);
        }
    };
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kElementPayload, flags)) {
        const UiPath from = FromPayload(p->Data);
        if (!IsUiPathPrefix(from, parent)) {
            indicate();
            if (p->IsDelivery()) {
                // O índice conta na lista sem o elemento movido: andar para baixo entre irmãos tira um.
                int target = index;
                if (target >= 0 && ParentOf(from) == parent && from.back() < target) --target;
                m_deferred.push_back([this, from, parent, target] { MoveElement(from, parent, target); });
            }
        }
    }
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kNewPayload, flags)) {
        indicate();
        if (p->IsDelivery()) {
            const auto type = static_cast<UiElementType>(*static_cast<const int*>(p->Data));
            m_deferred.push_back([this, type, parent, index] { AddElement(type, parent, index); });
        }
    }
    ImGui::EndDragDropTarget();
}

void UiDocEditor::DrawPreview() {
    if (!RenderPreview()) return;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const glm::vec2 res(m_resolution);
    const float zoom = m_zoomChoice == 0 ? std::max(0.01f, std::min(avail.x / res.x, avail.y / res.y))
                                         : static_cast<float>(m_zoomChoice) / 100.0f;
    const ImVec2 size(res.x * zoom, res.y * zoom);
    // Centralizada quando cabe.
    ImVec2 cursor = ImGui::GetCursorPos();
    if (size.x < avail.x) cursor.x += (avail.x - size.x) * 0.5f;
    if (size.y < avail.y) cursor.y += (avail.y - size.y) * 0.5f;
    ImGui::SetCursorPos(cursor);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const auto texture = static_cast<ImTextureID>(static_cast<uintptr_t>(m_framebuffer->ColorTexture()));
    ImGui::Image(ImTextureRef(texture), size, ImVec2(0, 1), ImVec2(1, 0));  // V invertido, como a janela Jogo
    // Botão invisível por cima: o clique é da prévia (não arrasta a janela) e ela aceita o que vem da paleta.
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##previa", ImVec2(std::max(size.x, 1.0f), std::max(size.y, 1.0f)));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetMousePos();
    const glm::vec2 point{(mouse.x - origin.x) / zoom, (mouse.y - origin.y) / zoom};

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kNewPayload)) {
            const auto type = static_cast<UiElementType>(*static_cast<const int*>(p->Data));
            const UiPath at = HitTest(point).value_or(UiPath{});
            const auto [parent, index] = InsertTarget(m_state.doc.elements, at);
            m_deferred.push_back([this, type, parent, index] { AddElement(type, parent, index); });
        }
        ImGui::EndDragDropTarget();
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < m_layout.size(); ++i) {
        if (!m_layoutPaths[i].empty()) DrawRect(dl, origin, zoom, m_layout[i].box, kOutlineColor, 1.0f);
    }
    if (hovered && m_resizeHandles == 0) {
        UiBox box;
        if (const auto hit = HitTest(point); hit && *hit != m_state.selected && BoxOf(*hit, box)) {
            DrawRect(dl, origin, zoom, box, kHoverColor, 1.0f);
        }
    }

    // Clique: alça do selecionado (redimensionar) ou seleção do elemento de cima.
    if (ImGui::IsItemActivated()) {
        int sides = 0;
        UiBox box;
        if (BoxOf(m_state.selected, box)) {
            const ImVec2 a(origin.x + box.min.x * zoom, origin.y + box.min.y * zoom);
            const ImVec2 b(origin.x + box.max.x * zoom, origin.y + box.max.y * zoom);
            for (const HandleSpot& h : HandlesOf(a, b)) {
                if (std::abs(mouse.x - h.at.x) <= kHandleSize && std::abs(mouse.y - h.at.y) <= kHandleSize) sides = h.sides;
            }
        }
        if (sides != 0) {
            if (const UiElement* e = UiElementAt(m_state.doc.elements, m_state.selected)) {
                const float scale = UiScale(res);
                m_resizePath = m_state.selected;
                m_resizeHandles = sides;
                m_resizeStart = *e;
                m_resizeBox = {box.min / scale, box.max / scale};
                m_resizeMouse = {mouse.x, mouse.y};
            }
        } else {
            Select(HitTest(point).value_or(UiPath{}));
        }
    }
    DrawPreviewHandles(glm::vec2(origin.x, origin.y), zoom);
}

void UiDocEditor::DrawPreviewHandles(const glm::vec2& origin2, float zoom) {
    const ImVec2 origin(origin2.x, origin2.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mouse = ImGui::GetMousePos();

    // Arrastando uma alça: o elemento volta ao começo do arraste e recebe a caixa nova (um passo de desfazer no fim).
    if (m_resizeHandles != 0) {
        UiElement* e = UiElementAt(m_state.doc.elements, m_resizePath);
        if (!e || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_resizeHandles = 0;
            Commit();
        } else {
            const glm::vec2 d = (glm::vec2(mouse.x, mouse.y) - m_resizeMouse) / (zoom * UiScale(glm::vec2(m_resolution)));
            UiBox to = m_resizeBox;
            if (m_resizeHandles & kLeft) to.min.x = std::min(to.min.x + d.x, to.max.x);
            if (m_resizeHandles & kRight) to.max.x = std::max(to.max.x + d.x, to.min.x);
            if (m_resizeHandles & kTop) to.min.y = std::min(to.min.y + d.y, to.max.y);
            if (m_resizeHandles & kBottom) to.max.y = std::max(to.max.y + d.y, to.min.y);
            *e = m_resizeStart;
            ResizeUiElement(*e, m_resizeBox, to);
            Changed();
            ImGui::SetMouseCursor(CursorFor(m_resizeHandles));
        }
    }

    UiBox box;
    if (!BoxOf(m_state.selected, box)) return;
    DrawRect(dl, origin, zoom, box, kSelectedColor, 2.0f);
    const ImVec2 a(origin.x + box.min.x * zoom, origin.y + box.min.y * zoom);
    const ImVec2 b(origin.x + box.max.x * zoom, origin.y + box.max.y * zoom);
    for (const HandleSpot& h : HandlesOf(a, b)) {
        const float s = kHandleSize * 0.5f;
        dl->AddRectFilled(ImVec2(h.at.x - s, h.at.y - s), ImVec2(h.at.x + s, h.at.y + s), kSelectedColor);
        dl->AddRect(ImVec2(h.at.x - s, h.at.y - s), ImVec2(h.at.x + s, h.at.y + s), IM_COL32(0, 0, 0, 200));
        if (m_resizeHandles == 0 && std::abs(mouse.x - h.at.x) <= kHandleSize && std::abs(mouse.y - h.at.y) <= kHandleSize &&
            ImGui::IsWindowHovered()) {
            ImGui::SetMouseCursor(CursorFor(h.sides));
        }
    }
}

void UiDocEditor::DrawProperties() {
    UiElement* e = UiElementAt(m_state.doc.elements, m_state.selected);
    if (!e) {
        ImGui::TextDisabled("Selecione um elemento na árvore ou na prévia.");
        return;
    }
    EditorContext& ctx = m_host.ctx;
    const float labelWidth = 110.0f;
    auto label = [&](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-FLT_MIN);
    };

    label("Tipo");
    int type = static_cast<int>(e->type);
    const char* types[] = {"Painel", "Texto", "Imagem", "Botão"};
    if (ImGui::Combo("##tipo", &type, types, 4)) {
        e->type = static_cast<UiElementType>(type);
        Changed();
    }
    label("Nome");
    if (ImGui::InputText("##nome", &e->name)) Changed();
    UiPath other;
    if (!e->name.empty() && FindUiElementPath(m_state.doc.elements, e->name, other) && other != m_state.selected) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Outro elemento já tem esse nome.");
    }
    if (m_classesFor != m_state.selected) {
        m_classesFor = m_state.selected;
        m_classesBuffer = JoinUiClassList(e->classes);
    }
    label("Classes");
    if (ImGui::InputText("##classes", &m_classesBuffer)) {
        e->classes = ParseUiClassList(m_classesBuffer);
        Changed();
    }
    ImGui::SetItemTooltip("Separadas por espaço (\"menu grande\"); as regras .menu das folhas valem para ele");
    if (e->type == UiElementType::Text || e->type == UiElementType::Button) {
        label("Texto");
        if (ImGui::InputTextMultiline("##texto", &e->text, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3.5f))) Changed();
    }
    label("Visível");
    if (ImGui::Checkbox("##visivel", &e->visible)) Changed();
    label("Interativo");
    if (ImGui::Checkbox("##interativo", &e->interactable)) Changed();
    ImGui::SetItemTooltip("Botão clicável (falso: :disabled)");
    if (e->type == UiElementType::Image || e->type == UiElementType::Panel || e->type == UiElementType::Button) {
        label("Imagem");
        const std::string current = e->image.path.empty() ? e->image.guid : e->image.path;
        if (const auto chosen = AssetCombo(ctx, "##imagem", AssetType::Texture, e->image.IsDefault() ? nullptr : &current,
                                           "(cor sólida)")) {
            e->image = {};
            if (!chosen->empty()) {
                e->image.guid = ctx.assets.Database().GuidFor(ctx.assets.FileForKey(*chosen));
                e->image.path = *chosen;
            }
            Changed(false, true);
        }
    }

    ImGui::SeparatorText("Estilo do elemento");
    ImGui::Checkbox("Só as declaradas no elemento", &m_showDeclaredOnly);
    ImGui::TextDisabled("Em cinza: o valor calculado (tipo, folhas, pai). Mudar declara no elemento.");
    const int fontProp = FindUiStyleProperty("font");
    for (const PropertyGroup& group : PropertyGroups()) {
        if (!ImGui::CollapsingHeader(group.title, ImGuiTreeNodeFlags_DefaultOpen)) continue;
        if (!ImGui::BeginTable(group.title, 3, ImGuiTableFlags_SizingStretchProp)) continue;
        ImGui::TableSetupColumn("nome", ImGuiTableColumnFlags_WidthFixed, labelWidth);
        ImGui::TableSetupColumn("valor", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("tirar", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        for (const char* name : group.names) {
            const int prop = FindUiStyleProperty(name);
            if (prop < 0) continue;
            const UiStyleValue* declared = e->inlineStyle.Find(prop);
            if (m_showDeclaredOnly && !declared) continue;
            UiStyleValue value = declared ? *declared : GetUiStyleValue(*e, prop);
            ImGui::PushID(prop);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (declared) {
                ImGui::TextUnformatted(name);
            } else {
                ImGui::TextDisabled("%s", name);
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (EditStyleValue(ctx, prop, value)) {
                e->SetStyleValue(name, value);
                Changed(false, prop == fontProp);
            }
            ImGui::TableNextColumn();
            if (declared && ImGui::SmallButton("x")) {
                e->ClearStyle(name);
                Changed(false, prop == fontProp);
            }
            if (declared) ImGui::SetItemTooltip("Tirar a declaração (volta ao valor das folhas e do tipo)");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void UiDocEditor::DrawStyles() {
    EditorContext& ctx = m_host.ctx;
    const UiElement* selected = UiElementAt(m_state.doc.elements, m_state.selected);

    ImGui::SeparatorText("Folhas do documento");
    for (size_t si = 0; si < m_state.sheets.size(); ++si) {
        ImGui::PushID(static_cast<int>(si));
        ImGui::BulletText("%s%s", m_state.sheets[si].key.c_str(), m_state.sheets[si].file.empty() ? " (não abriu)" : "");
        ImGui::SameLine();
        if (ImGui::SmallButton("Tirar")) {
            m_deferred.push_back([this, si] {
                if (si >= m_state.sheets.size()) return;
                m_state.sheets.erase(m_state.sheets.begin() + static_cast<std::ptrdiff_t>(si));
                m_state.doc.styles.erase(m_state.doc.styles.begin() + static_cast<std::ptrdiff_t>(si));
                m_selectorBuffers.clear();
                Changed(true);
                Commit();
            });
        }
        ImGui::SetItemTooltip("Tira a folha do documento (o arquivo .mstyle continua)");
        ImGui::PopID();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (const auto chosen = AssetCombo(ctx, "##addfolha", AssetType::UiStyle, nullptr, nullptr)) {
        if (!chosen->empty()) {
            const fs::path file = ctx.assets.FileForKey(*chosen);
            UiFont ref{ctx.assets.Database().GuidFor(file), *chosen};
            UiEditSheet sheet;
            sheet.key = *chosen;
            std::string error;
            if (LoadUiStyleSheet(file, sheet.sheet, &error)) {
                sheet.file = file.lexically_normal();
                m_state.doc.styles.push_back(ref);
                m_state.sheets.push_back(std::move(sheet));
                Changed(true);
                Commit();
            } else {
                m_error = std::format("Folha {}: {}", *chosen, error);
            }
        }
    }
    ImGui::SetItemTooltip("Acrescentar uma folha .mstyle do projeto ao documento");

    if (!m_selectorError.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", m_selectorError.c_str());
    if (selected) {
        ImGui::TextDisabled("* = vale para o selecionado (%s)", selected->name.empty() ? "sem nome" : selected->name.c_str());
    }

    for (size_t si = 0; si < m_state.sheets.size(); ++si) {
        UiEditSheet& sheet = m_state.sheets[si];
        if (sheet.file.empty()) continue;
        ImGui::PushID(static_cast<int>(si) + 1000);
        if (ImGui::CollapsingHeader(sheet.key.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for (size_t ri = 0; ri < sheet.sheet.rules.size(); ++ri) {
                UiStyleRule& rule = sheet.sheet.rules[ri];
                ImGui::PushID(static_cast<int>(ri));
                ImGui::Separator();
                const bool matches = selected && std::any_of(rule.selectors.begin(), rule.selectors.end(), [&](const UiSelector& s) {
                    return s.Matches(*selected, UiElementStates(*selected));
                });
                ImGui::TextUnformatted(matches ? "*" : " ");
                ImGui::SameLine();
                std::string& buffer = m_selectorBuffers.try_emplace({si, ri}, rule.selector).first->second;
                ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - 70.0f));
                ImGui::InputText("##seletor", &buffer);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    std::string error;
                    if (SetUiRuleSelector(rule, buffer, &error)) {
                        m_selectorError.clear();
                        Changed(true);
                    } else {
                        m_selectorError = std::format("Seletor \"{}\": {}", buffer, error);
                        buffer = rule.selector;
                    }
                }
                ImGui::SetItemTooltip("Seletor: Tipo.classe#nome:estado, lista com vírgula");
                ImGui::SameLine();
                if (ImGui::SmallButton("Excluir")) {
                    m_deferred.push_back([this, si, ri] {
                        if (si >= m_state.sheets.size() || ri >= m_state.sheets[si].sheet.rules.size()) return;
                        auto& rules = m_state.sheets[si].sheet.rules;
                        rules.erase(rules.begin() + static_cast<std::ptrdiff_t>(ri));
                        m_selectorBuffers.clear();
                        Changed(true);
                        Commit();
                    });
                }
                int removeProp = -1;
                if (ImGui::BeginTable("decl", 3, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("nome", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                    ImGui::TableSetupColumn("valor", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("tirar", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
                    for (UiStyleDeclaration& d : rule.declarations.items) {
                        ImGui::PushID(d.property);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted(UiStylePropertyName(d.property));
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (EditStyleValue(ctx, d.property, d.value)) Changed(true);
                        ImGui::TableNextColumn();
                        if (ImGui::SmallButton("x")) removeProp = d.property;
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                if (removeProp >= 0) {
                    rule.declarations.Remove(UiStylePropertyName(removeProp));
                    Changed(true);
                }
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##addprop", "+ propriedade")) {
                    const UiElement defaults = UiElement::Make(selected ? selected->type : UiElementType::Panel);
                    for (int p = 0; p < UiStylePropertyCount(); ++p) {
                        if (rule.declarations.Find(p)) continue;
                        if (ImGui::Selectable(UiStylePropertyName(p))) {
                            // Começa com o valor calculado do selecionado (ou o padrão do tipo).
                            rule.declarations.Set(p, selected ? GetUiStyleValue(*selected, p) : GetUiStyleValue(defaults, p));
                            Changed(true);
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }
            if (ImGui::Button("+ Regra")) {
                UiStyleRule rule;
                const std::string selector = selected && !selected->name.empty() ? "#" + selected->name : std::string("*");
                SetUiRuleSelector(rule, selector);
                sheet.sheet.rules.push_back(std::move(rule));
                m_selectorBuffers.clear();
                Changed(true);
                Commit();
            }
            ImGui::SetItemTooltip("Regra nova (com o #nome do selecionado; mude o seletor no campo)");
        }
        ImGui::PopID();
    }
}

void UiDocEditor::DrawSavePopup() {
    if (m_openSavePopup) {
        ImGui::OpenPopup(kSaveTitle);
        m_openSavePopup = false;
    }
    if (!ImGui::BeginPopupModal(kSaveTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("%s tem alterações não salvas (o documento ou as folhas).", ToUtf8(m_file.filename()).c_str());
    if (ImGui::Button("Salvar", ImVec2(110, 0))) {
        ImGui::CloseCurrentPopup();
        FinishPending(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Descartar", ImVec2(110, 0))) {
        ImGui::CloseCurrentPopup();
        FinishPending(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
        m_pending = Pending::None;
        m_pendingThen = {};
    }
    ImGui::EndPopup();
}

void UiDocEditor::HandleShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        std::string error;
        if (IsOpen() && !Save(&error)) m_error = "Não foi possível gravar: " + error;
    }
    if (!IsOpen() || m_resizeHandles != 0) return;
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, true)) {
        if (io.KeyShift) {
            Redo();
        } else {
            Undo();
        }
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, true)) Redo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && !m_state.selected.empty()) {
        DuplicateElement(m_state.selected);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !m_state.selected.empty()) DeleteElement(m_state.selected);
}

} // namespace editor
