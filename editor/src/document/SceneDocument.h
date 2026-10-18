#pragma once

#include "EditorContext.h"
#include "EditorSettings.h"
#include "panels/ProjectPanel.h"
#include "UndoHistory.h"

#include <motor/Motor.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace editor {

// Pasta sugerida para novos projetos: Documentos do usuário.
std::filesystem::path DefaultProjectsDir();

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
        std::function<void()> projectClosing;    // antes de trocar de projeto (descarrega os scripts)
        std::function<void()> projectOpened;     // projeto novo aberto, antes de abrir a cena (carrega os scripts)
        std::function<void()> exportGame;        // Arquivo > Exportar jogo (Fase 10)
    };

    enum class FileAction { None, NewScene, OpenScene, OpenScenePath, NewProject, OpenProject, OpenProjectPath, Quit };

    SceneDocument(EditorContext& ctx, UndoHistory& history, EditorSettings& settings, ProjectPanel& projectPanel,
                  Host host);

    motor::Project* GetProject() { return m_project ? &*m_project : nullptr; }
    const std::filesystem::path& ScenePath() const { return m_scenePath; }

    // Pergunta antes se houver alterações não salvas. `path` é usado pelas ações *Path.
    void RequestFileAction(FileAction action, const std::filesystem::path& path = {});

    bool SaveScene(bool saveAs);                // false se falhou ou o usuário cancelou

    // Sem diálogos nem popups (servidor MCP): o erro volta em `error`. Com alterações não salvas, abrir ou
    // criar cena só segue com `discardChanges`. SaveSceneTo com `path` vazio grava no arquivo atual.
    bool OpenSceneDirect(const std::filesystem::path& path, bool discardChanges, std::string& error);
    bool NewSceneDirect(bool discardChanges, std::string& error);
    bool SaveSceneTo(const std::filesystem::path& path, std::string& error);
    bool OpenProjectAt(const std::filesystem::path& fileOrDir, bool restoreLastScene,
                       bool allowNewerVersion = false);
    // Arquivo > Novo projeto sem popup (--create, ferramenta MCP project_create): cria <location>/<name> com a
    // pasta Scripts/, o script de exemplo e a cena inicial, e abre. Não pergunta por alterações não salvas.
    void StopPlay() { m_host.stopPlay(); }
    bool CreateProjectAt(const std::filesystem::path& location, const std::string& name, std::string& error);
    bool CreateEmptySceneFile(const std::filesystem::path& path);
    void OnPathMoved(const std::filesystem::path& from, const std::filesystem::path& to);
    void OnPathDeleted(const std::filesystem::path& path);

    void MarkSaved();
    bool HasUnsavedChanges();                   // compara a cena atual com o último estado salvo
    void PollChanges(float dt);                 // atualiza o "*" do título e grava o salvamento automático
    void SaveRecovery(std::string_view reason = "autosave", const motor::Scene* overrideScene = nullptr);
    void ClearRecovery();
    void ShowError(std::string message);

    void DrawFileMenu();
    void DrawPopups();

private:
    void PerformFileAction(FileAction action, const std::filesystem::path& path);
    bool OpenSceneAt(const std::filesystem::path& path);
    void ReplaceScene(motor::Scene&& scene, const std::filesystem::path& path);
    void RememberScene();                       // guarda a cena atual como "última" do projeto
    void CreateProject(const std::filesystem::path& location, const std::string& name);
    void OfferNewProjectInEmptyDir(const std::filesystem::path& emptyDir);
    void ApplyRecovery(const motor::SceneSerializer::RecoveryInfo& recovery);
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
    float m_autoSaveTimer = 0.0f;

    FileAction m_pendingAction = FileAction::None;
    std::filesystem::path m_pendingPath;
    bool m_openUnsavedPopup = false;
    bool m_openNewProjectPopup = false;
    std::string m_newProjectName;
    std::string m_newProjectLocation;  // UTF-8
    std::string m_newProjectEmptyDir;  // não vazio quando abriu uma pasta vazia

    bool m_openNewerVersionPopup = false;
    std::filesystem::path m_pendingNewerProjectPath;
    bool m_pendingNewerProjectRestoreScene = true;
    std::string m_pendingNewerProjectName;
    std::string m_pendingNewerProjectVersion;

    bool m_openRecoveryPopup = false;
    motor::SceneSerializer::RecoveryInfo m_pendingRecovery;

    std::string m_errorMessage;
    bool m_openErrorPopup = false;
};

} // namespace editor
