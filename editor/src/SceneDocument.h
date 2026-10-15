#pragma once

#include "EditorContext.h"
#include "EditorSettings.h"
#include "ProjectPanel.h"
#include "UndoHistory.h"

#include <motor/Motor.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace editor {

// Projeto e cena abertos no editor: abrir/salvar/criar, detecção de alterações não salvas, menu Arquivo,
// popups de confirmação, de novo projeto e de erro, e o título da janela.
class SceneDocument {
public:
    // O que o documento precisa do resto do editor.
    struct Host {
        std::function<bool()> isPlaying;
        std::function<void()> stopPlay;
        std::function<void()> quit;              // fecha sem perguntar
        std::function<void()> requestClose;      // passa por OnCloseRequested
        std::function<void(const std::string&)> setWindowTitle;
    };

    enum class FileAction { None, NewScene, OpenScene, OpenScenePath, NewProject, OpenProject, OpenProjectPath, Quit };

    SceneDocument(EditorContext& ctx, UndoHistory& history, EditorSettings& settings, ProjectPanel& projectPanel,
                  Host host);

    motor::Project* GetProject() { return m_project ? &*m_project : nullptr; }
    const std::filesystem::path& ScenePath() const { return m_scenePath; }

    // Pergunta antes se houver alterações não salvas. `path` é usado pelas ações *Path.
    void RequestFileAction(FileAction action, const std::filesystem::path& path = {});

    bool SaveScene(bool saveAs);                // false se falhou ou o usuário cancelou
    bool OpenProjectAt(const std::filesystem::path& fileOrDir, bool restoreLastScene);
    bool CreateEmptySceneFile(const std::filesystem::path& path);
    void OnPathMoved(const std::filesystem::path& from, const std::filesystem::path& to);
    void OnPathDeleted(const std::filesystem::path& path);

    void MarkSaved();
    bool HasUnsavedChanges();                   // compara a cena atual com o último estado salvo
    void PollChanges(float dt);                 // atualiza o "*" do título de tempos em tempos
    void ShowError(std::string message);

    void DrawFileMenu();
    void DrawPopups();

private:
    void PerformFileAction(FileAction action, const std::filesystem::path& path);
    bool OpenSceneAt(const std::filesystem::path& path);
    void ReplaceScene(motor::Scene&& scene, const std::filesystem::path& path);
    void RememberScene();                       // guarda a cena atual como "última" do projeto
    void CreateProject(const std::filesystem::path& location, const std::string& name);
    std::filesystem::path DefaultSceneDir() const;
    void UpdateWindowTitle();
    void DrawNewProjectPopup();

    EditorContext& m_ctx;
    UndoHistory& m_history;
    EditorSettings& m_settings;
    ProjectPanel& m_projectPanel;
    Host m_host;

    std::optional<motor::Project> m_project;
    std::filesystem::path m_scenePath;   // vazio = "Sem título"
    std::string m_savedSnapshot;         // JSON do último estado salvo/carregado
    bool m_dirty = false;
    float m_dirtyCheckTimer = 0.0f;

    FileAction m_pendingAction = FileAction::None;
    std::filesystem::path m_pendingPath;
    bool m_openUnsavedPopup = false;
    bool m_openNewProjectPopup = false;
    std::string m_newProjectName;
    std::string m_newProjectLocation;  // UTF-8
    std::string m_errorMessage;
    bool m_openErrorPopup = false;
};

} // namespace editor
