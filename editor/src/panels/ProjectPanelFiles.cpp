// Painel Projeto: abrir, renomear, criar, copiar, colar e mover arquivos.

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
// Operações
// ---------------------------------------------------------------------------

namespace {
// Código C++ (scripts e headers): abre no editor de código do projeto, na pasta do projeto.
bool IsCodeFile(const fs::path& path) {
    std::string ext = ToUtf8(path.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" || ext == ".hpp" || ext == ".inl";
}
} // namespace

void ProjectPanel::Activate(const Entry& entry, const Callbacks& callbacks) {
    if (entry.isDirectory) {
        SetCurrentDir(entry.path);
    } else if (entry.type == AssetType::Scene) {
        callbacks.openScene(entry.path);
    } else if ((entry.type == AssetType::Material || entry.type == AssetType::Prefab) && callbacks.selectAsset) {
        callbacks.selectAsset(entry.path);
    } else if (IsCodeFile(entry.path) && callbacks.openScript) {
        callbacks.openScript(entry.path);
    } else {
        Shell::OpenWithDefaultApp(entry.path);
    }
}

void ProjectPanel::BeginRename(const fs::path& path) {
    std::error_code ec;
    m_renaming = path;
    SelectOnly(path);
    m_renameBuffer = ToUtf8(fs::is_directory(path, ec) ? path.filename() : path.stem());
    m_renameFocus = true;
}

void ProjectPanel::CommitRename(const Callbacks& callbacks) {
    const fs::path from = m_renaming;
    m_renaming.clear();

    if (std::string problem = motor::Project::ValidateName(m_renameBuffer); !problem.empty()) {
        ShowError(std::move(problem));
        return;
    }

    std::error_code ec;
    const bool isDir = fs::is_directory(from, ec);
    // A extensão é preservada: o usuário edita só o nome.
    const fs::path to = from.parent_path() / PathFromUtf8(m_renameBuffer + (isDir ? "" : ToUtf8(from.extension())));
    if (to == from) return;
    // No Windows, mudar só maiúsculas/minúsculas aponta para o mesmo arquivo: é permitido.
    if (fs::exists(to, ec) && !fs::equivalent(from, to, ec)) {
        ShowError(std::format("Já existe \"{}\" nesta pasta.", ToUtf8(to.filename())));
        return;
    }

    fs::rename(from, to, ec);
    if (ec) {
        ShowError(std::format("Não foi possível renomear: {}", ec.message()));
        return;
    }
    MoveMetaAlong(from, to);
    SelectOnly(to);
    m_currentDir = motor::RebasePath(m_currentDir, from, to);
    if (callbacks.pathMoved) callbacks.pathMoved(from, to);
    Refresh();
}

void ProjectPanel::CreateFolder() {
    const fs::path path = UniquePath(m_currentDir, "Nova pasta", "");
    std::error_code ec;
    if (!fs::create_directory(path, ec)) {
        ShowError(std::format("Não foi possível criar a pasta: {}", ec.message()));
        return;
    }
    m_search.clear();
    Refresh();
    BeginRename(path);
}

void ProjectPanel::CreateScene(const Callbacks& callbacks) {
    const fs::path path = UniquePath(m_currentDir, "Nova cena", motor::SceneSerializer::kFileExtension);
    if (!callbacks.createSceneFile || !callbacks.createSceneFile(path)) return;
    m_search.clear();
    Refresh();
    BeginRename(path);
}

void ProjectPanel::CreateMaterial(const Callbacks& callbacks) {
    const fs::path path = UniquePath(m_currentDir, "Novo material", motor::kMaterialFileExtension);
    if (!callbacks.createMaterialFile || !callbacks.createMaterialFile(path)) return;
    m_search.clear();
    Refresh();
    BeginRename(path);
    if (callbacks.selectAsset) callbacks.selectAsset(path);
}

void ProjectPanel::CopySelection(bool cut) {
    if (m_selection.empty()) return;
    m_clipboard = m_selection;
    m_clipboardCut = cut;
    // Também vai para a área de transferência do Windows, para colar no Explorer.
    Clipboard::SetFiles(m_clipboard, cut);
    m_clipboardSequence = Clipboard::SequenceNumber();
}

void ProjectPanel::Paste(const Callbacks& callbacks) {
    // Se a área de transferência mudou depois do nosso Ctrl+C/X, quem manda é ela (ex.: arquivos copiados no Explorer).
    if (Clipboard::SequenceNumber() != m_clipboardSequence) {
        const std::vector<fs::path> external = Clipboard::GetFiles();
        if (!external.empty()) {
            CopyItems(external, m_currentDir);
            return;
        }
    }
    if (m_clipboard.empty()) return;

    if (m_clipboardCut) {
        MoveItems(m_clipboard, m_currentDir, callbacks);
        m_clipboard.clear();  // recortar vale para uma colagem só
        m_clipboardCut = false;
    } else {
        CopyItems(m_clipboard, m_currentDir);
    }
}

void ProjectPanel::DuplicateSelection() {
    std::vector<fs::path> created;
    std::vector<std::string> errors;
    for (const fs::path& source : m_selection) {
        const fs::path to = UniqueDestination(source, source.parent_path());
        std::error_code ec;
        fs::copy(source, to, fs::copy_options::recursive, ec);
        if (!ec) CopyCompanions(source, to, ec);
        if (ec) {
            errors.push_back(std::format("Não foi possível duplicar \"{}\": {}", ToUtf8(source.filename()), ec.message()));
        } else {
            created.push_back(to);
        }
    }
    if (!created.empty()) m_selection = created;
    ShowErrors(errors);
    Refresh();
}

void ProjectPanel::CopyItems(const std::vector<fs::path>& sources, const fs::path& folder) {
    std::vector<fs::path> created;
    std::vector<std::string> errors;
    std::error_code ec;
    for (const fs::path& source : sources) {
        const std::string name = ToUtf8(source.filename());
        if (!fs::exists(source, ec)) {
            errors.push_back(std::format("\"{}\" não existe mais.", name));
            continue;
        }
        if (fs::is_directory(source, ec) && IsInside(folder, source)) {
            errors.push_back(std::format("Não é possível copiar \"{}\" para dentro dela mesma.", name));
            continue;
        }
        const fs::path to = UniqueDestination(source, folder);
        fs::copy(source, to, fs::copy_options::recursive, ec);
        if (!ec) CopyCompanions(source, to, ec);
        if (ec) {
            errors.push_back(std::format("Não foi possível copiar \"{}\": {}", name, ec.message()));
            continue;
        }
        motor::log::Info("Copiado: {} -> {}", ToUtf8(source), ToUtf8(to));
        created.push_back(to);
    }
    if (!created.empty()) {
        if (!SamePath(folder, m_currentDir)) SetCurrentDir(folder);
        m_selection = created;
    }
    ShowErrors(errors);
    Refresh();
}

void ProjectPanel::MoveItems(const std::vector<fs::path>& sources, const fs::path& folder, const Callbacks& callbacks) {
    std::vector<fs::path> moved;
    std::vector<std::string> errors;
    std::error_code ec;
    for (const fs::path& source : sources) {
        const std::string name = ToUtf8(source.filename());
        if (SamePath(source, folder) || IsInside(folder, source)) {
            errors.push_back(std::format("Não é possível mover \"{}\" para dentro dela mesma.", name));
            continue;
        }
        const fs::path to = folder / source.filename();
        if (SamePath(to, source)) continue;  // já está nessa pasta
        if (fs::exists(to, ec)) {
            errors.push_back(std::format("Já existe \"{}\" em \"{}\".", name, ToUtf8(folder.filename())));
            continue;
        }
        fs::rename(source, to, ec);
        if (ec) {
            errors.push_back(std::format("Não foi possível mover \"{}\": {}", name, ec.message()));
            continue;
        }
        MoveMetaAlong(source, to);
        m_currentDir = motor::RebasePath(m_currentDir, source, to);
        if (callbacks.pathMoved) callbacks.pathMoved(source, to);
        moved.push_back(to);
    }
    if (!moved.empty()) m_selection = moved;
    ShowErrors(errors);
    Refresh();
}

void ProjectPanel::AcceptMoveDrop(const fs::path& folder, const Callbacks& callbacks) {
    const std::vector<fs::path> paths = AcceptAnyPathDrop();
    if (!paths.empty()) MoveItems(paths, folder, callbacks);
    // Entidade arrastada da Hierarquia: vira um prefab nesta pasta.
    if (callbacks.createPrefabFromEntity && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload)) {
            callbacks.createPrefabFromEntity(*static_cast<const uint32_t*>(payload->Data), folder);
            Refresh();
        }
        ImGui::EndDragDropTarget();
    }
}

void ProjectPanel::ShowError(std::string message) {
    motor::log::Warn("Projeto: {}", message);
    m_error = std::move(message);
    m_openErrorPopup = true;
}

void ProjectPanel::ShowErrors(const std::vector<std::string>& errors) {
    if (errors.empty()) return;
    std::string message;
    const size_t shown = std::min<size_t>(errors.size(), 6);
    for (size_t i = 0; i < shown; ++i) message += (i ? "\n" : "") + errors[i];
    if (errors.size() > shown) message += std::format("\n... e mais {} erro(s).", errors.size() - shown);
    ShowError(std::move(message));
}

} // namespace editor
