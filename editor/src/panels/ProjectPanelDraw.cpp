// Painel Projeto: desenho (barra, árvore, lista e grade, menus, popups) e atalhos.

#include "panels/ProjectPanel.h"
#include "panels/ProjectPanelUtil.h"

#include "EditorContext.h"
#include "FileDialog.h"

#include <motor/assets/AssetDatabase.h>
#include <motor/core/Log.h>
#include <motor/core/Paths.h>
#include <motor/project/Project.h>
#include <motor/renderer/Texture.h>
#include <motor/scene/SceneSerializer.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <format>

namespace editor {

namespace fs = std::filesystem;
using motor::AssetType;
using motor::IsInside;
using motor::PathFromUtf8;
using motor::ToUtf8;
using namespace project_panel;

// ---------------------------------------------------------------------------
// Desenho
// ---------------------------------------------------------------------------

void ProjectPanel::DrawToolbar(const Callbacks& callbacks) {
    const bool atRoot = SamePath(m_currentDir, m_assetsDir);
    ImGui::BeginDisabled(atRoot && m_search.empty());
    if (ImGui::ArrowButton("##up", ImGuiDir_Up)) SetCurrentDir(atRoot ? m_currentDir : m_currentDir.parent_path());
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Pasta acima (Backspace)");

    // Trilha de navegação: Assets > Scenes > ... (cada parte é clicável e aceita arquivos arrastados).
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    auto crumb = [&](const std::string& label, const fs::path& target, int id) {
        ImGui::PushID(id);
        if (ImGui::Button(label.c_str())) SetCurrentDir(target);
        AcceptMoveDrop(target, callbacks);
        ImGui::PopID();
    };
    ImGui::SameLine();
    if (!m_search.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Busca em %s", motor::Project::kAssetsFolder);
    } else {
        crumb(motor::Project::kAssetsFolder, m_assetsDir, 0);
        fs::path accum = m_assetsDir;
        int id = 1;
        for (const fs::path& part : m_currentDir.lexically_relative(m_assetsDir)) {
            if (part == ".") continue;
            accum /= part;
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::TextDisabled(">");
            ImGui::SameLine(0.0f, 2.0f);
            crumb(ToUtf8(part), accum, id++);
        }
    }
    ImGui::PopStyleColor();

    // À direita: busca, filtro de tipo, modo de exibição e "+ Criar".
    const ImGuiStyle& style = ImGui::GetStyle();
    const float searchWidth = ImGui::GetFontSize() * 11.0f;
    const float filterWidth = ImGui::GetFontSize() * 6.5f;
    const float buttonsWidth = ImGui::CalcTextSize("Ver").x + ImGui::CalcTextSize("+ Criar").x + style.FramePadding.x * 4.0f;
    const float rightWidth = searchWidth + filterWidth + buttonsWidth + style.ItemSpacing.x * 3.0f;
    ImGui::SameLine();
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > rightWidth) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - rightWidth);

    if (m_focusSearch) {
        ImGui::SetKeyboardFocusHere();
        m_focusSearch = false;
    }
    ImGui::SetNextItemWidth(searchWidth);
    if (ImGui::InputTextWithHint("##search", "Buscar (Ctrl+F)", &m_search, ImGuiInputTextFlags_EscapeClearsAll)) {
        m_selection.clear();
        Refresh();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(filterWidth);
    if (ImGui::BeginCombo("##filter", FilterName(static_cast<int>(m_filter)))) {
        for (int f = 0; f <= static_cast<int>(Filter::Prefabs); ++f) {
            if (ImGui::Selectable(FilterName(f), static_cast<int>(m_filter) == f)) {
                m_filter = static_cast<Filter>(f);
                Refresh();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Mostrar só um tipo de arquivo");

    ImGui::SameLine();
    if (ImGui::Button("Ver")) ImGui::OpenPopup("##project_view");
    if (ImGui::BeginPopup("##project_view")) {
        if (ImGui::RadioButton("Lista", !m_view.grid)) m_view.grid = false;
        if (ImGui::RadioButton("Grade", m_view.grid)) m_view.grid = true;
        ImGui::BeginDisabled(!m_view.grid);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        ImGui::SliderFloat("Tamanho", &m_view.iconSize, 40.0f, 160.0f, "%.0f px");
        ImGui::EndDisabled();
        ImGui::TextDisabled("Ctrl + roda do mouse também muda o tamanho.");
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("+ Criar")) ImGui::OpenPopup("##project_create");
    if (ImGui::BeginPopup("##project_create")) {
        DrawCreateMenuItems(callbacks);
        ImGui::EndPopup();
    }
}

void ProjectPanel::DrawFolderTree(const fs::path& dir, const Callbacks& callbacks) {
    const std::vector<fs::path> subfolders = Subfolders(dir);  // cópia: o cache pode mudar durante a recursão

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (subfolders.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (SamePath(dir, m_currentDir) && m_search.empty()) flags |= ImGuiTreeNodeFlags_Selected;
    if (SamePath(dir, m_assetsDir)) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    // Ao navegar pela lista, abre o caminho até a pasta atual.
    if (m_revealCurrent && !SamePath(dir, m_currentDir) && IsInside(m_currentDir, dir)) ImGui::SetNextItemOpen(true);

    const std::string name = SamePath(dir, m_assetsDir) ? motor::Project::kAssetsFolder : ToUtf8(dir.filename());
    ImGui::PushID(ToUtf8(dir).c_str());
    // Espaços no rótulo reservam lugar para o ícone desenhado por cima.
    const bool open = ImGui::TreeNodeEx("##folder", flags, "      %s", name.c_str());
    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const float iconSize = ImGui::GetFontSize() * 0.9f;
    DrawIcon(ImGui::GetWindowDrawList(),
             ImVec2(itemMin.x + ImGui::GetTreeNodeToLabelSpacing(),
                    itemMin.y + (ImGui::GetItemRectSize().y - iconSize) * 0.5f),
             iconSize, Icon::Folder);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) SetCurrentDir(dir);
    if (ImGui::IsItemHovered()) m_hoveredFolder = dir;
    AcceptMoveDrop(dir, callbacks);
    ImGui::PopID();

    if (open) {
        for (const fs::path& sub : subfolders) DrawFolderTree(sub, callbacks);
        ImGui::TreePop();
    }
}

void ProjectPanel::DrawContents(const Callbacks& callbacks) {
    const bool searching = !m_search.empty();
    if (searching) {
        ImGui::TextDisabled(m_searchTruncated ? "Mais de %zu resultados (mostrando os primeiros)" : "%zu resultado(s)",
                            m_entries.size());
    } else if (m_entries.empty()) {
        ImGui::TextDisabled(m_filter == Filter::All ? "Pasta vazia. Clique com o botão direito para criar algo, ou "
                                                      "arraste arquivos do Explorer para cá."
                                                    : "Nenhum arquivo desse tipo nesta pasta.");
    }

    if (m_view.grid) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float cellWidth = m_view.iconSize + style.FramePadding.x * 2.0f;
        const float avail = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, static_cast<int>((avail + style.ItemSpacing.x) / (cellWidth + style.ItemSpacing.x)));
        for (size_t i = 0; i < m_entries.size(); ++i) {
            if (i % static_cast<size_t>(columns) != 0) ImGui::SameLine();
            DrawGridCell(m_entries[i], cellWidth, callbacks);
        }
    } else {
        const int columnCount = searching ? 4 : 3;
        if (ImGui::BeginTable("##entries", columnCount, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Nome", ImGuiTableColumnFlags_WidthStretch);
            if (searching) ImGui::TableSetupColumn("Pasta", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn("Tipo", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Arquivo .xxxx").x);
            ImGui::TableSetupColumn("Tamanho", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("000.0 MB").x);
            for (const Entry& e : m_entries) DrawListRow(e, callbacks);
            ImGui::EndTable();
        }
    }

    // Área vazia: clique limpa a seleção; botão direito cria itens; aceita itens arrastados (move para a pasta atual).
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##contents_background", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 30.0f)));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) m_selection.clear();
    if (!searching) AcceptMoveDrop(m_currentDir, callbacks);
    if (ImGui::BeginPopupContextItem("##contents_ctx")) {
        DrawCreateMenuItems(callbacks);
        ImGui::Separator();
        if (ImGui::MenuItem("Colar", "Ctrl+V")) Paste(callbacks);
        if (ImGui::MenuItem("Selecionar tudo", "Ctrl+A")) {
            m_selection.clear();
            for (const Entry& e : m_entries) m_selection.push_back(e.path);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Mostrar no Explorer")) Shell::RevealInExplorer(m_currentDir);
        if (ImGui::MenuItem("Atualizar")) Refresh();
        ImGui::EndPopup();
    }
}

void ProjectPanel::DrawEntryIcon(const Entry& e, float x, float y, float size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool cut = m_clipboardCut && std::any_of(m_clipboard.begin(), m_clipboard.end(),
                                                   [&](const fs::path& p) { return SamePath(p, e.path); });
    const float alpha = cut ? 0.45f : 1.0f;

    if (motor::Texture2D* thumb = GetThumbnail(e)) {
        // Miniatura com a proporção da imagem, centralizada no quadrado.
        const float aspect = static_cast<float>(thumb->Width()) / static_cast<float>(thumb->Height());
        const ImVec2 extent = aspect >= 1.0f ? ImVec2(size, size / aspect) : ImVec2(size * aspect, size);
        const ImVec2 p0{x + (size - extent.x) * 0.5f, y + (size - extent.y) * 0.5f};
        const auto id = static_cast<ImTextureID>(static_cast<uintptr_t>(thumb->NativeHandle()));
        dl->AddRectFilled(p0, ImVec2(p0.x + extent.x, p0.y + extent.y), IM_COL32(40, 40, 46, 255));
        dl->AddImage(ImTextureRef(id), p0, ImVec2(p0.x + extent.x, p0.y + extent.y), ImVec2(0, 1), ImVec2(1, 0),
                     Alpha(IM_COL32_WHITE, alpha));
        return;
    }
    DrawIcon(dl, ImVec2(x, y), size, IconFor(e.isDirectory, e.type), alpha);
}

void ProjectPanel::DrawRenameField(float width, const Callbacks& callbacks) {
    ImGui::SetNextItemWidth(width);
    if (m_renameFocus) {
        ImGui::SetKeyboardFocusHere();
        m_renameFocus = false;
    }
    const bool commit = ImGui::InputText("##rename", &m_renameBuffer,
                                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    if (commit) {
        CommitRename(callbacks);
    } else if (ImGui::IsItemDeactivated()) {
        m_renaming.clear();  // Esc ou clique fora cancela
    }
}

void ProjectPanel::DrawListRow(const Entry& e, const Callbacks& callbacks) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushID(ToUtf8(e.path).c_str());

    const float rowHeight = ImGui::GetFrameHeight();
    const float iconSize = ImGui::GetFontSize();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float iconY = start.y + (rowHeight - iconSize) * 0.5f;
    const float textX = start.x + iconSize + 8.0f;

    if (SamePath(m_renaming, e.path)) {
        DrawEntryIcon(e, start.x + 2.0f, iconY, iconSize);
        ImGui::SetCursorScreenPos(ImVec2(textX, start.y));
        DrawRenameField(-FLT_MIN, callbacks);
    } else {
        const bool clicked = ImGui::Selectable("##item", IsSelected(e.path),
                                               ImGuiSelectableFlags_AllowDoubleClick |
                                                   ImGuiSelectableFlags_SpanAllColumns,
                                               ImVec2(0.0f, rowHeight));
        ItemBehavior(e, clicked, callbacks);

        DrawEntryIcon(e, start.x + 2.0f, iconY, iconSize);
        const bool isOpenScene = !e.isDirectory && SamePath(m_currentScene, e.path);
        const std::string label = isOpenScene ? e.name + "  (aberta)" : e.name;
        const ImU32 color = isOpenScene ? IM_COL32(120, 190, 255, 255) : ImGui::GetColorU32(ImGuiCol_Text);
        ImGui::GetWindowDrawList()->AddText(ImVec2(textX, start.y + (rowHeight - ImGui::GetFontSize()) * 0.5f), color,
                                            label.c_str());
    }

    if (!m_search.empty()) {
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", e.location.empty() ? motor::Project::kAssetsFolder : e.location.c_str());
    }

    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    if (e.isDirectory) {
        ImGui::TextDisabled("Pasta");
    } else if (e.type != AssetType::Unknown) {
        ImGui::TextDisabled("%s", motor::AssetTypeName(e.type));
    } else {
        ImGui::TextDisabled("Arquivo %s", e.extension.c_str());
    }

    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    if (!e.isDirectory) ImGui::TextDisabled("%s", FormatSize(e.size).c_str());

    ImGui::PopID();
}

void ProjectPanel::DrawGridCell(const Entry& e, float cellWidth, const Callbacks& callbacks) {
    ImGui::PushID(ToUtf8(e.path).c_str());
    const ImGuiStyle& style = ImGui::GetStyle();
    const float iconSize = m_view.iconSize;
    const float labelHeight = ImGui::GetTextLineHeight();
    const float cellHeight = style.FramePadding.y * 3.0f + iconSize + labelHeight;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float iconX = start.x + (cellWidth - iconSize) * 0.5f;
    const float iconY = start.y + style.FramePadding.y;
    const float labelY = iconY + iconSize + style.FramePadding.y;

    if (SamePath(m_renaming, e.path)) {
        ImGui::BeginGroup();
        DrawEntryIcon(e, iconX, iconY, iconSize);
        ImGui::Dummy(ImVec2(cellWidth, labelY - start.y - style.ItemSpacing.y));
        DrawRenameField(cellWidth, callbacks);
        ImGui::EndGroup();
    } else {
        const bool clicked = ImGui::Selectable("##cell", IsSelected(e.path), ImGuiSelectableFlags_AllowDoubleClick,
                                               ImVec2(cellWidth, cellHeight));
        ItemBehavior(e, clicked, callbacks);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("%s%s\n%s", e.name.c_str(), e.extension.c_str(),
                              e.isDirectory ? "Pasta" : motor::AssetTypeName(e.type));
        }

        DrawEntryIcon(e, iconX, iconY, iconSize);
        const bool isOpenScene = !e.isDirectory && SamePath(m_currentScene, e.path);
        const std::string label = Ellipsize(e.name, cellWidth - 4.0f);
        const float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(start.x + (cellWidth - labelWidth) * 0.5f, labelY),
            isOpenScene ? IM_COL32(120, 190, 255, 255) : ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
    }
    ImGui::PopID();
}

void ProjectPanel::ItemBehavior(const Entry& e, bool clicked, const Callbacks& callbacks) {
    if (clicked) {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            SelectOnly(e.path);
            Activate(e, callbacks);
            return;  // Activate pode trocar de pasta e invalidar a lista
        }
        ClickSelect(e.path);
        if (callbacks.selectAsset && m_selection.size() == 1) callbacks.selectAsset(e.path);
    }
    if (e.isDirectory && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) m_hoveredFolder = e.path;

    if (ImGui::BeginDragDropSource()) {
        // Arrastar um item selecionado leva a seleção inteira.
        const std::vector<fs::path> dragged = IsSelected(e.path) ? m_selection : std::vector<fs::path>{e.path};
        SetPathDragPayload(dragged);
        if (dragged.size() == 1) {
            ImGui::TextUnformatted(e.name.c_str());
        } else {
            ImGui::Text("%zu itens", dragged.size());
        }
        ImGui::EndDragDropSource();
    }
    if (e.isDirectory) AcceptMoveDrop(e.path, callbacks);

    DrawItemContextMenu(e, callbacks);
}

void ProjectPanel::DrawItemContextMenu(const Entry& e, const Callbacks& callbacks) {
    if (!ImGui::BeginPopupContextItem("##item_ctx")) return;
    if (!IsSelected(e.path)) SelectOnly(e.path);
    const bool single = m_selection.size() == 1;

    if (single) {
        if (e.isDirectory && ImGui::MenuItem("Abrir pasta", "Enter")) Activate(e, callbacks);
        if (e.type == AssetType::Scene && ImGui::MenuItem("Abrir cena", "Enter")) Activate(e, callbacks);
        if (e.type == AssetType::Mesh && callbacks.addMeshToScene && ImGui::MenuItem("Adicionar à cena")) {
            callbacks.addMeshToScene(e.path);
        }
        if (e.type == AssetType::Texture && callbacks.applyTexture &&
            ImGui::MenuItem("Aplicar à entidade selecionada")) {
            callbacks.applyTexture(e.path);
        }
        if (e.type == AssetType::Prefab && callbacks.addPrefabToScene && ImGui::MenuItem("Adicionar à cena")) {
            callbacks.addPrefabToScene(e.path);
        }
        if (e.type == AssetType::Model && callbacks.addModelToScene && ImGui::MenuItem("Adicionar à cena")) {
            callbacks.addModelToScene(e.path);
        }
        if (e.type == AssetType::Model && callbacks.reimportModel && ImGui::MenuItem("Importar de novo")) {
            callbacks.reimportModel(e.path);
        }
        const bool inspectable = e.type == AssetType::Material || e.type == AssetType::Prefab;
        if (inspectable && callbacks.selectAsset && ImGui::MenuItem("Editar no Inspector", "Enter")) {
            callbacks.selectAsset(e.path);
        }
        if (!e.isDirectory && e.type != AssetType::Scene && !inspectable &&
            ImGui::MenuItem("Abrir com programa padrão", "Enter")) {
            Shell::OpenWithDefaultApp(e.path);
        }
        if (!m_search.empty() && ImGui::MenuItem("Mostrar na pasta")) {
            const fs::path path = e.path;
            SetCurrentDir(path.parent_path());
            SelectOnly(path);
        }
        ImGui::Separator();
    }
    if (ImGui::MenuItem("Recortar", "Ctrl+X")) CopySelection(true);
    if (ImGui::MenuItem("Copiar", "Ctrl+C")) CopySelection(false);
    if (ImGui::MenuItem("Colar", "Ctrl+V")) Paste(callbacks);
    if (ImGui::MenuItem("Duplicar", "Ctrl+D")) DuplicateSelection();
    ImGui::Separator();
    if (single && ImGui::MenuItem("Renomear", "F2")) BeginRename(e.path);
    if (ImGui::MenuItem(single ? "Excluir" : "Excluir selecionados", "Del")) {
        m_pendingDelete = m_selection;
        m_openDeletePopup = true;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Mostrar no Explorer")) Shell::RevealInExplorer(e.path);
    ImGui::EndPopup();
}

void ProjectPanel::DrawCreateMenuItems(const Callbacks& callbacks) {
    if (ImGui::MenuItem("Nova pasta")) CreateFolder();
    if (ImGui::MenuItem("Nova cena")) CreateScene(callbacks);
    if (ImGui::MenuItem("Novo material")) CreateMaterial(callbacks);
    if (callbacks.createScript && ImGui::MenuItem("Novo script...")) callbacks.createScript();
    ImGui::TextDisabled("Prefab: arraste uma entidade da Hierarquia para uma pasta");
}

void ProjectPanel::DrawPopups(const Callbacks& callbacks) {
    const char* deleteTitle = "Excluir##projeto";
    const char* errorTitle = "Erro##projeto";
    if (m_openDeletePopup) {
        ImGui::OpenPopup(deleteTitle);
        m_openDeletePopup = false;
    }
    if (m_openErrorPopup) {
        ImGui::OpenPopup(errorTitle);
        m_openErrorPopup = false;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    const ImVec2 buttonSize{110.0f, 0.0f};

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(deleteTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::error_code ec;
        const bool anyDir = std::any_of(m_pendingDelete.begin(), m_pendingDelete.end(),
                                        [&](const fs::path& p) { return fs::is_directory(p, ec); });
        if (m_pendingDelete.size() == 1) {
            ImGui::Text("Enviar \"%s\" para a Lixeira?", ToUtf8(m_pendingDelete.front().filename()).c_str());
        } else {
            ImGui::Text("Enviar %zu itens para a Lixeira?", m_pendingDelete.size());
        }
        if (anyDir) ImGui::TextDisabled("Pastas são movidas com todo o conteúdo.");
        ImGui::Spacing();

        if (ImGui::Button("Excluir", buttonSize)) {
            std::vector<std::string> errors;
            for (const fs::path& path : m_pendingDelete) {
                std::string error;
                if (!Shell::MoveToTrash(path, &error)) {
                    errors.push_back(std::move(error));
                    continue;
                }
                motor::log::Info("Enviado para a Lixeira: {}", ToUtf8(path));
                if (const fs::path meta = motor::AssetDatabase::MetaPathFor(path); fs::exists(meta, ec)) {
                    Shell::MoveToTrash(meta);
                }
                if (IsInside(m_currentDir, path)) m_currentDir = path.parent_path();
                if (callbacks.pathDeleted) callbacks.pathDeleted(path);
            }
            m_selection.clear();
            m_pendingDelete.clear();
            ShowErrors(errors);
            Refresh();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_pendingDelete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(errorTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(m_error.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("OK", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void ProjectPanel::HandleKeys(const Callbacks& callbacks) {
    if (!m_focused || !m_renaming.empty() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
    const ImGuiIO& io = ImGui::GetIO();

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) m_focusSearch = true;
    if (io.WantTextInput) return;  // digitando na busca

    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            m_selection.clear();
            for (const Entry& e : m_entries) m_selection.push_back(e.path);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) Paste(callbacks);
        if (m_selection.empty()) return;
        if (ImGui::IsKeyPressed(ImGuiKey_C, false)) CopySelection(false);
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) CopySelection(true);
        if (ImGui::IsKeyPressed(ImGuiKey_D, false)) DuplicateSelection();
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && m_search.empty() && !SamePath(m_currentDir, m_assetsDir)) {
        SetCurrentDir(m_currentDir.parent_path());
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_selection.clear();
    if (m_selection.empty()) return;

    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) BeginRename(m_selection.front());
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        m_pendingDelete = m_selection;
        m_openDeletePopup = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
        if (const Entry* e = FindEntry(m_selection.front())) {
            const Entry copy = *e;  // Activate pode reconstruir a lista
            Activate(copy, callbacks);
        }
    }
}

motor::Texture2D* ProjectPanel::GetThumbnail(const Entry& e) {
    if (e.isDirectory || e.type != AssetType::Texture) return nullptr;

    auto it = m_thumbnails.find(e.path);
    if (it != m_thumbnails.end() && it->second.modified == e.modified) return it->second.texture.get();
    if (m_thumbnailBudget <= 0) return it != m_thumbnails.end() ? it->second.texture.get() : nullptr;
    --m_thumbnailBudget;

    // Miniatura em espaço linear (RGBA8): o ImGui desenha os bytes como estão, sem conversão sRGB.
    Thumbnail thumb;
    thumb.modified = e.modified;
    motor::ImageData image;
    std::string error;
    if (motor::LoadImageFile(e.path, image, &error)) {
        const motor::ImageData small = Downscale(image, kThumbnailPixels);
        thumb.texture = std::make_shared<motor::Texture2D>(small.width, small.height, small.pixels.data(),
                                                           motor::TextureFilter::Linear,
                                                           motor::TextureColorSpace::Linear);
    } else {
        motor::log::Warn("Miniatura: {}", error);
    }
    return (m_thumbnails[e.path] = std::move(thumb)).texture.get();
}

} // namespace editor
