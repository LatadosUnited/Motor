// Painel Projeto: estado, varredura das pastas e seleção. Desenho em ProjectPanelDraw.cpp,
// operações de arquivo em ProjectPanelFiles.cpp.

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

void ProjectPanel::Draw(const motor::Project* project, const fs::path& currentScene, const Callbacks& callbacks) {
    m_focused = false;
    m_hoveredFolder.clear();
    m_thumbnailBudget = 1;

    if (!project) m_projectRoot.clear();
    else if (!SamePath(project->RootDir(), m_projectRoot)) ResetFor(project);

    if (!ImGui::Begin("Projeto")) {
        ImGui::End();
        FlushImports();
        return;
    }
    m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    if (!project) {
        ImGui::Spacing();
        ImGui::TextDisabled("Nenhum projeto aberto.");
        ImGui::Spacing();
        if (ImGui::Button("Criar projeto...")) callbacks.createProject();
        ImGui::SameLine();
        if (ImGui::Button("Abrir projeto...")) callbacks.openProject();
        FlushImports();
        DrawPopups(callbacks);
        ImGui::End();
        return;
    }

    m_currentScene = currentScene;
    // Mudanças feitas fora do editor: a cada kRescanInterval, mas só relê a pasta se o sistema avisou que algo mudou
    // (reler a cada segundo custava dezenas de ms com o disco lento, Fase 27).
    if (!m_needsRefresh && ImGui::GetTime() - m_lastScanTime > kRescanInterval) {
        m_lastScanTime = ImGui::GetTime();
        if (m_watcher.Folder() != m_assetsDir) m_watcher.Watch(m_assetsDir);
        m_needsRefresh = m_watcher.Changed();
    }
    if (m_needsRefresh) Rescan();

    DrawToolbar(callbacks);
    ImGui::Separator();

    const ImGuiTableFlags layoutFlags =
        ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##project_layout", 2, layoutFlags)) {
        ImGui::TableSetupColumn("pastas", ImGuiTableColumnFlags_WidthStretch, 0.25f);
        ImGui::TableSetupColumn("conteudo", ImGuiTableColumnFlags_WidthStretch, 0.75f);
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##tree")) {
            DrawFolderTree(m_assetsDir, callbacks);
            m_revealCurrent = false;
        }
        ImGui::EndChild();

        ImGui::TableNextColumn();
        // Ctrl+roda muda o tamanho dos ícones na grade em vez de rolar.
        const bool zooming = m_view.grid && ImGui::GetIO().KeyCtrl;
        if (ImGui::BeginChild("##contents", ImVec2(0, 0), ImGuiChildFlags_None,
                              zooming ? ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None)) {
            if (zooming && ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
                m_view.iconSize = std::clamp(m_view.iconSize + ImGui::GetIO().MouseWheel * 8.0f, 40.0f, 160.0f);
            }
            DrawContents(callbacks);
        }
        ImGui::EndChild();

        ImGui::EndTable();
    }

    HandleKeys(callbacks);
    FlushImports();
    DrawPopups(callbacks);
    ImGui::End();
}

void ProjectPanel::QueueImport(std::vector<fs::path> files) {
    m_pendingImports.insert(m_pendingImports.end(), files.begin(), files.end());
}

void ProjectPanel::FlushImports() {
    if (m_pendingImports.empty()) return;
    const std::vector<fs::path> files = std::exchange(m_pendingImports, {});
    if (m_projectRoot.empty()) {
        ShowError("Abra ou crie um projeto para importar arquivos.");
        return;
    }
    const fs::path target = m_hoveredFolder.empty() ? m_currentDir : m_hoveredFolder;
    CopyItems(files, target);
}

void ProjectPanel::ResetFor(const motor::Project* project) {
    m_projectRoot = project->RootDir();
    m_assetsDir = project->AssetsDir().lexically_normal();
    m_currentDir = m_assetsDir;
    m_selection.clear();
    m_anchor.clear();
    m_clipboard.clear();
    m_renaming.clear();
    m_pendingDelete.clear();
    m_search.clear();
    m_thumbnails.clear();
    m_needsRefresh = true;
    m_revealCurrent = true;
}

bool ProjectPanel::PassesFilter(const Entry& e) const {
    switch (m_filter) {
        case Filter::Scenes: return !e.isDirectory && e.type == AssetType::Scene;
        case Filter::Textures: return !e.isDirectory && e.type == AssetType::Texture;
        case Filter::Meshes: return !e.isDirectory && (e.type == AssetType::Mesh || e.type == AssetType::Model);
        case Filter::Materials: return !e.isDirectory && e.type == AssetType::Material;
        case Filter::Prefabs: return !e.isDirectory && e.type == AssetType::Prefab;
        case Filter::All: break;
    }
    return true;
}

void ProjectPanel::Rescan() {
    m_needsRefresh = false;
    m_lastScanTime = ImGui::GetTime();
    m_subfolderCache.clear();
    m_searchTruncated = false;

    std::error_code ec;
    if (!fs::is_directory(m_currentDir, ec) || !IsInside(m_currentDir, m_assetsDir)) m_currentDir = m_assetsDir;

    auto makeEntry = [&](const fs::directory_entry& de, Entry& e) {
        e.path = de.path();
        const std::string filename = ToUtf8(e.path.filename());
        if (filename.empty() || filename[0] == '.') return false;  // ocultos
        e.isDirectory = de.is_directory(ec);
        if (e.isDirectory) {
            e.name = filename;
        } else {
            e.extension = ToUtf8(e.path.extension());
            // Temporário do salvamento atômico e .meta (o GUID do asset) não aparecem: o .meta acompanha o arquivo.
            if (e.extension == ".tmp" || motor::AssetDatabase::IsMetaFile(e.path)) return false;
            e.name = ToUtf8(e.path.stem());
            e.type = motor::AssetTypeFromPath(e.path);
            e.size = de.file_size(ec);
            e.modified = de.last_write_time(ec);
        }
        return true;
    };

    m_entries.clear();
    if (m_search.empty()) {
        for (const auto& de : fs::directory_iterator(m_currentDir, fs::directory_options::skip_permission_denied, ec)) {
            Entry e;
            if (!makeEntry(de, e)) continue;
            if (e.isDirectory || PassesFilter(e)) m_entries.push_back(std::move(e));
        }
    } else {
        // Busca em todo o Assets, pelo nome (sem diferenciar maiúsculas).
        const std::string needle = Lower(m_search);
        for (auto it = fs::recursive_directory_iterator(m_assetsDir, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            Entry e;
            if (!makeEntry(*it, e)) {
                if (it->is_directory(ec)) it.disable_recursion_pending();  // não entra em pastas ocultas
                continue;
            }
            if (Lower(ToUtf8(e.path.filename())).find(needle) == std::string::npos || !PassesFilter(e)) continue;
            const fs::path parent = e.path.parent_path().lexically_relative(m_assetsDir);
            e.location = parent == "." ? std::string() : motor::ToGenericUtf8(parent);
            m_entries.push_back(std::move(e));
            if (m_entries.size() >= kMaxSearchResults) {
                m_searchTruncated = true;
                break;
            }
        }
    }
    std::sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory;
        const std::string la = Lower(a.name), lb = Lower(b.name);
        if (la != lb) return la < lb;
        return a.extension != b.extension ? a.extension < b.extension : a.location < b.location;
    });

    std::erase_if(m_selection, [&](const fs::path& p) { return !fs::exists(p, ec); });
}

const std::vector<fs::path>& ProjectPanel::Subfolders(const fs::path& dir) {
    auto it = m_subfolderCache.find(dir);
    if (it != m_subfolderCache.end()) return it->second;

    std::vector<fs::path> result;
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        const std::string name = ToUtf8(de.path().filename());
        if (de.is_directory(ec) && !name.empty() && name[0] != '.') result.push_back(de.path());
    }
    std::sort(result.begin(), result.end(), [](const fs::path& a, const fs::path& b) {
        return Lower(ToUtf8(a.filename())) < Lower(ToUtf8(b.filename()));
    });
    return m_subfolderCache.emplace(dir, std::move(result)).first->second;
}

void ProjectPanel::SetCurrentDir(const fs::path& dir) {
    if (!IsInside(dir, m_assetsDir)) return;
    m_currentDir = dir.lexically_normal();
    m_search.clear();  // navegar sai da busca
    m_selection.clear();
    m_renaming.clear();
    m_needsRefresh = true;
    m_revealCurrent = true;
}

// ---------------------------------------------------------------------------
// Seleção
// ---------------------------------------------------------------------------

bool ProjectPanel::IsSelected(const fs::path& path) const {
    return std::any_of(m_selection.begin(), m_selection.end(), [&](const fs::path& p) { return SamePath(p, path); });
}

void ProjectPanel::SelectOnly(const fs::path& path) {
    m_selection = {path};
    m_anchor = path;
}

void ProjectPanel::ClickSelect(const fs::path& path) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyShift && !m_anchor.empty()) {
        auto indexOf = [&](const fs::path& p) {
            for (size_t i = 0; i < m_entries.size(); ++i) {
                if (SamePath(m_entries[i].path, p)) return static_cast<ptrdiff_t>(i);
            }
            return ptrdiff_t{-1};
        };
        const ptrdiff_t a = indexOf(m_anchor), b = indexOf(path);
        if (a >= 0 && b >= 0) {
            if (!io.KeyCtrl) m_selection.clear();
            for (ptrdiff_t i = std::min(a, b); i <= std::max(a, b); ++i) {
                const fs::path& p = m_entries[static_cast<size_t>(i)].path;
                if (!IsSelected(p)) m_selection.push_back(p);
            }
            return;
        }
    }
    if (io.KeyCtrl) {
        if (IsSelected(path)) {
            std::erase_if(m_selection, [&](const fs::path& p) { return SamePath(p, path); });
        } else {
            m_selection.push_back(path);
        }
        m_anchor = path;
        return;
    }
    SelectOnly(path);
}

const ProjectPanel::Entry* ProjectPanel::FindEntry(const fs::path& path) const {
    for (const Entry& e : m_entries) {
        if (SamePath(e.path, path)) return &e;
    }
    return nullptr;
}

} // namespace editor
