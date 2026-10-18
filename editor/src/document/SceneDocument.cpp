// Documento do editor: ações com confirmação e cenas (abrir, salvar, recuperação). Projetos em
// SceneDocumentProject.cpp; menu Arquivo e popups em SceneDocumentUi.cpp.

#include "document/SceneDocument.h"

#include "FileDialog.h"
#include "scripts/ScriptProject.h"

#include <imgui.h>
#include <motor/core/Version.h>
#include <imgui_stdlib.h>

#include <cstdlib>
#include <format>
#include <fstream>
#include <utility>

namespace editor {

using namespace motor;
namespace fs = std::filesystem;

fs::path DefaultProjectsDir() {
    std::error_code ec;
#ifdef _WIN32
    if (const wchar_t* profile = _wgetenv(L"USERPROFILE")) {
        const fs::path docs = fs::path(profile) / "Documents";
        if (fs::is_directory(docs, ec)) return docs;
        return profile;
    }
#else
    if (const char* home = std::getenv("HOME")) return home;
#endif
    return fs::current_path(ec);
}

SceneDocument::SceneDocument(EditorContext& ctx, UndoHistory& history, EditorSettings& settings,
                             ProjectPanel& projectPanel, Host host)
    : m_ctx(ctx), m_history(history), m_settings(settings), m_projectPanel(projectPanel), m_host(std::move(host)) {}

// ---------------------------------------------------------------------------
// Ações (com confirmação de alterações não salvas)
// ---------------------------------------------------------------------------

void SceneDocument::RequestFileAction(FileAction action, const fs::path& path) {
    m_host.stopPlay();  // trocar de cena ou de projeto encerra o Play (e a pergunta de salvar é sobre a cena original)
    if (HasUnsavedChanges()) {
        m_pendingAction = action;
        m_pendingPath = path;
        m_openUnsavedPopup = true;
        return;
    }
    PerformFileAction(action, path);
}

void SceneDocument::PerformFileAction(FileAction action, const fs::path& path) {
    switch (action) {
        case FileAction::NewScene:
            ClearRecovery();
            ReplaceScene(Scene{}, {});
            log::Info("Nova cena");
            break;
        case FileAction::OpenScene:
            if (const auto chosen = FileDialog::OpenFile(FileDialog::Kind::Scene, DefaultSceneDir())) {
                if (OpenSceneAt(*chosen)) ClearRecovery();
            }
            break;
        case FileAction::OpenScenePath:
            if (OpenSceneAt(path)) ClearRecovery();
            break;
        case FileAction::NewProject:
            m_newProjectEmptyDir.clear();
            m_openNewProjectPopup = true;
            break;
        case FileAction::OpenProject: {
            const fs::path start = m_project ? m_project->RootDir().parent_path() : DefaultProjectsDir();
            if (const auto chosen = FileDialog::OpenFile(FileDialog::Kind::Project, start)) OpenProjectAt(*chosen, true);
            break;
        }
        case FileAction::OpenProjectPath:
            OpenProjectAt(path, true);
            break;
        case FileAction::Quit:
            m_host.quit();
            break;
        case FileAction::None:
            break;
    }
}

// ---------------------------------------------------------------------------
// Cenas
// ---------------------------------------------------------------------------

fs::path SceneDocument::DefaultSceneDir() const {
    if (!m_scenePath.empty()) return m_scenePath.parent_path();
    if (m_project) {
        std::error_code ec;
        const fs::path scenes = m_project->AssetsDir() / "Scenes";
        return fs::is_directory(scenes, ec) ? scenes : m_project->AssetsDir();
    }
    return {};
}

bool SceneDocument::SaveScene(bool saveAs) {
    if (m_host.isPlaying()) {
        // Salvar agora gravaria o estado do jogo, que é descartado ao parar.
        ShowError("Pare o Play antes de salvar (Ctrl+P). Alterações feitas durante o Play são descartadas.");
        return false;
    }
    fs::path path = m_scenePath;
    if (saveAs || path.empty()) {
        const fs::path suggestion = path.empty() ? DefaultSceneDir() / "Nova cena.mscene" : path;
        const auto chosen = FileDialog::SaveFile(FileDialog::Kind::Scene, suggestion);
        if (!chosen) return false;
        path = *chosen;
    }

    std::string error;
    if (!SceneSerializer::Save(path, m_ctx.scene, m_ctx.assets, &error)) {
        ShowError(error);
        return false;
    }
    m_scenePath = path;
    MarkSaved();
    ClearRecovery();

    if (m_project) {
        if (!IsInside(path, m_project->RootDir())) {
            log::Warn("A cena foi salva fora do projeto '{}'", m_project->Name());
        } else if (m_project->StartupScene().empty()) {
            // Primeira cena salva no projeto vira a cena inicial.
            m_project->SetStartupScene(path);
            m_project->Save();
        }
    }
    RememberScene();
    m_projectPanel.Refresh();
    return true;
}

bool SceneDocument::OpenSceneDirect(const fs::path& path, bool discardChanges, std::string& error) {
    m_host.stopPlay();
    if (!discardChanges && HasUnsavedChanges()) {
        error = "a cena aberta tem alterações não salvas: salve antes ou peça para descartá-las";
        return false;
    }
    Scene loaded;
    if (!SceneSerializer::Load(path, loaded, m_ctx.assets, &error)) return false;
    ClearRecovery();
    ReplaceScene(std::move(loaded), path);
    RememberScene();
    return true;
}

bool SceneDocument::NewSceneDirect(bool discardChanges, std::string& error) {
    m_host.stopPlay();
    if (!discardChanges && HasUnsavedChanges()) {
        error = "a cena aberta tem alterações não salvas: salve antes ou peça para descartá-las";
        return false;
    }
    ClearRecovery();
    ReplaceScene(Scene{}, {});
    log::Info("Nova cena");
    return true;
}

bool SceneDocument::SaveSceneTo(const fs::path& path, std::string& error) {
    if (m_host.isPlaying()) {
        error = "pare o Play antes de salvar (as alterações do Play são descartadas)";
        return false;
    }
    const fs::path target = path.empty() ? m_scenePath : path;
    if (target.empty()) {
        error = "a cena ainda não tem arquivo: informe o caminho (ex.: Assets/Cenas/Fase1.mscene)";
        return false;
    }
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);  // ex.: Assets/Cenas ainda não existe no projeto
    if (!SceneSerializer::Save(target, m_ctx.scene, m_ctx.assets, &error)) return false;
    m_scenePath = target;
    MarkSaved();
    ClearRecovery();
    if (m_project && IsInside(target, m_project->RootDir()) && m_project->StartupScene().empty()) {
        m_project->SetStartupScene(target);
        m_project->Save();
    }
    RememberScene();
    m_projectPanel.Refresh();
    return true;
}

bool SceneDocument::OpenSceneAt(const fs::path& path) {
    Scene loaded;
    std::string error;
    if (!SceneSerializer::Load(path, loaded, m_ctx.assets, &error)) {
        ShowError(error);
        return false;
    }
    ReplaceScene(std::move(loaded), path);
    RememberScene();
    return true;
}

void SceneDocument::ReplaceScene(Scene&& scene, const fs::path& path) {
    m_ctx.scene = std::move(scene);
    m_ctx.selected = kNullEntity;
    m_ctx.pendingDelete = kNullEntity;
    m_ctx.eulerEntity = kNullEntity;
    m_scenePath = path;
    m_history.Reset(m_ctx.scene, kNullEntity);  // o histórico é de uma cena só
    MarkSaved();
}

void SceneDocument::MarkSaved() {
    m_savedSnapshot = SceneSerializer::ToJson(m_ctx.scene, m_ctx.assets, {.pathHints = false});
    m_dirty = false;
    m_autoSaveTimer = 0.0f;
    UpdateWindowTitle();
}

bool SceneDocument::HasUnsavedChanges() {
    const bool dirty = SceneSerializer::ToJson(m_ctx.scene, m_ctx.assets, {.pathHints = false}) != m_savedSnapshot;
    if (dirty != m_dirty) {
        m_dirty = dirty;
        if (!m_dirty) {
            m_autoSaveTimer = 0.0f;
            if (!m_host.isPlaying()) ClearRecovery();
        }
        UpdateWindowTitle();
    }
    return dirty;
}

void SceneDocument::PollChanges(float dt) {
    // Compara o JSON da cena com o salvo a cada meio segundo (cenas pequenas, custo baixo).
    // No Play a cena é a do jogo (descartada ao parar): não conta.
    if (m_host.isPlaying()) return;
    m_dirtyCheckTimer += dt;
    if (m_dirtyCheckTimer >= 0.5f) {
        m_dirtyCheckTimer = 0.0f;
        HasUnsavedChanges();
    }
    if (m_dirty) {
        m_autoSaveTimer += dt;
        constexpr float kAutoSaveIntervalSeconds = 120.0f;
        if (m_autoSaveTimer >= kAutoSaveIntervalSeconds) {
            m_autoSaveTimer = 0.0f;
            SaveRecovery("autosave");
        }
    }
}

void SceneDocument::SaveRecovery(std::string_view reason, const Scene* overrideScene) {
    if (!m_project) return;
    const Scene& scene = overrideScene ? *overrideScene : m_ctx.scene;
    std::string error;
    if (!SceneSerializer::SaveRecovery(m_project->RootDir(), scene, m_ctx.assets, m_scenePath, reason, &error)) {
        log::Warn("Não foi possível gravar a recuperação da cena: {}", error);
    }
}

void SceneDocument::ClearRecovery() {
    if (!m_project) return;
    SceneSerializer::ClearRecovery(m_project->RootDir());
}

void SceneDocument::RememberScene() {
    if (!m_project || !IsInside(m_scenePath, m_project->RootDir())) return;
    m_settings.lastScene = m_scenePath;
    m_settings.Save();
}

bool SceneDocument::CreateEmptySceneFile(const fs::path& path) {
    Scene empty;
    std::string error;
    if (!SceneSerializer::Save(path, empty, m_ctx.assets, &error)) {
        ShowError(error);
        return false;
    }
    return true;
}

} // namespace editor
