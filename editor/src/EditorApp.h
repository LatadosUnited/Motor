#pragma once

#include "EditorContext.h"
#include "EditorSettings.h"
#include "PlayMode.h"
#include "ProjectPanel.h"
#include "ScriptBuilder.h"
#include "ScriptHost.h"
#include "ScriptProject.h"
#include "UndoHistory.h"

#include <motor/Motor.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace editor {

class EditorApp final : public motor::Application {
public:
    // projectToOpen: pasta ou .mproject passado na linha de comando; vazio = reabre o último projeto.
    explicit EditorApp(std::filesystem::path projectToOpen = {});

private:
    void OnInit() override;
    void OnFixedUpdate(float dt) override;
    void OnUpdate(float dt) override;
    void OnRender(motor::Renderer& renderer) override;
    void OnImGui() override;
    void OnShutdown() override;
    bool OnCloseRequested() override;

    void BuildDefaultScene();
    void DrawMenuBar();
    void DrawViewport();
    void DrawGizmo(const ImVec2& origin);
    void DrawViewportToolbar(const ImVec2& position);
    void BuildDockLayout(unsigned int dockspaceId);
    void HandleShortcuts();
    void PickAt(const glm::vec2& uv);  // uv no viewport, (0,0) = canto superior esquerdo
    std::pair<glm::vec3, glm::vec3> ViewportRay(const glm::vec2& uv) const;  // origem, direção (não normalizada)
    void HandleViewportDrop(const ImVec2& origin);
    void FocusSelected();
    bool IsInteracting() const;  // arrastando/editando algo: a alteração ainda não terminou
    void Undo();
    void Redo();

    // --- Modo Play ---
    enum class PlayRequest { None, Play, Pause, Stop, Step };
    void ApplyPlayRequest(PlayRequest request);
    void StopPlay();  // volta à edição (nada acontece se já estiver editando)
    bool InGameView() const { return m_play.IsActive() && m_gameView; }  // viewport pela câmera do jogo
    void DrawPlayControls(const ImVec2& origin);

    // --- Arquivos de cena e projeto (EditorFile.cpp) ---
    enum class FileAction { None, NewScene, OpenScene, OpenScenePath, NewProject, OpenProject, OpenProjectPath, Quit };
    // Pergunta antes se houver alterações não salvas. `path` é usado pelas ações *Path.
    void RequestFileAction(FileAction action, const std::filesystem::path& path = {});
    void PerformFileAction(FileAction action, const std::filesystem::path& path);

    bool SaveScene(bool saveAs);                // false se falhou ou o usuário cancelou
    bool OpenSceneAt(const std::filesystem::path& path);
    void ReplaceScene(motor::Scene&& scene, const std::filesystem::path& path);
    void MarkSaved();
    bool HasUnsavedChanges();                   // compara a cena atual com o último estado salvo
    void RememberScene();                       // guarda a cena atual como "última" do projeto

    bool OpenProjectAt(const std::filesystem::path& fileOrDir, bool restoreLastScene);
    void CreateProject(const std::filesystem::path& location, const std::string& name);
    bool CreateEmptySceneFile(const std::filesystem::path& path);
    void OnPathMoved(const std::filesystem::path& from, const std::filesystem::path& to);
    void OnPathDeleted(const std::filesystem::path& path);
    std::filesystem::path DefaultSceneDir() const;

    void UpdateWindowTitle();
    void ShowError(std::string message);
    void DrawFileMenu();
    void DrawFilePopups();
    void DrawNewProjectPopup();
    void DrawProjectPanel();

    // --- Scripts do projeto (EditorScripts.cpp) ---
    void OpenProjectScripts();   // ao abrir um projeto: carrega a DLL já compilada e compila de novo
    void CloseProjectScripts();  // descarrega (troca de projeto, sair)
    void UpdateScripts(float dt);  // vigia a pasta Scripts, acompanha a compilação e recarrega
    void RequestScriptBuild();   // compila agora (ou logo que a compilação em andamento terminar)
    void ReloadScripts();        // carrega a DLL compilada; só fora do Play
    ScriptHost::ForEachScene AllScenes();  // cena aberta e fotos do desfazer
    void DrawScriptsMenu();
    void DrawScriptsPanel();
    void DrawScriptsStatus();    // aviso na barra de menus quando a compilação falha
    void DrawNewScriptPopup();

    EditorContext m_ctx;
    motor::Camera m_camera;
    motor::FlyCameraController m_cameraController{m_camera};
    std::unique_ptr<motor::Framebuffer> m_viewport;

    enum class GizmoTool { None, Translate, Rotate, Scale };
    GizmoTool m_gizmoTool = GizmoTool::Translate;
    bool m_gizmoLocal = false;  // false = eixos do mundo
    bool m_snap = false;
    float m_snapTranslate = 0.5f;
    float m_snapRotate = 15.0f;  // graus
    float m_snapScale = 0.1f;

    glm::vec2 m_viewportSize{1.0f};
    bool m_looking = false;       // botão direito segurado no viewport
    bool m_resetLayout = true;    // constrói o layout padrão se não houver um salvo
    bool m_showDemo = false;

    std::filesystem::path m_scenePath;   // vazio = "Sem título"
    std::string m_savedSnapshot;         // JSON do último estado salvo/carregado
    bool m_dirty = false;
    float m_dirtyCheckTimer = 0.0f;
    float m_assetRefreshTimer = 0.0f;
    FileAction m_pendingAction = FileAction::None;
    std::filesystem::path m_pendingPath;
    bool m_openUnsavedPopup = false;

    std::filesystem::path m_startupProject;
    std::optional<motor::Project> m_project;
    EditorSettings m_settings;
    ProjectPanel m_projectPanel;
    UndoHistory m_history;
    int m_historyRequest = 0;  // -1 desfazer, +1 refazer (executado no início do próximo frame)

    ScriptBuilder m_scriptBuilder;
    ScriptHost m_scriptHost;
    std::optional<ScriptProject::Toolchain> m_toolchain;
    std::string m_toolchainError;
    std::string m_scriptSignature;       // estado da pasta Scripts na última verificação
    std::filesystem::file_time_type m_loadedDllTime{};  // data da DLL carregada (recompilou sem mudar = não recarrega)
    float m_scriptWatchTimer = 0.0f;
    bool m_scriptBuildQueued = false;    // a pasta mudou durante a compilação: compila de novo ao terminar
    bool m_scriptReloadPending = false;  // compilou durante o Play: recarrega ao parar
    std::string m_scriptLog;             // cópia da saída da compilação para o painel
    std::vector<ScriptProject::Diagnostic> m_scriptDiagnostics;
    bool m_focusScriptsPanel = false;
    bool m_openNewScriptPopup = false;
    std::string m_newScriptName;

    PlayMode m_play;
    PlayRequest m_playRequest = PlayRequest::None;  // aplicado no início do próximo frame, como o desfazer
    bool m_gameView = true;          // no Play: true = câmera do jogo, false = câmera livre do editor
    bool m_viewportFocused = false;  // o jogo só recebe teclado/mouse com o viewport em foco
    bool m_focusViewport = false;
    bool m_missingGameCamera = false;  // no Play sem câmera na cena: mostra pela câmera do editor
    bool m_openNewProjectPopup = false;
    std::string m_newProjectName;
    std::string m_newProjectLocation;  // UTF-8
    std::string m_errorMessage;
    bool m_openErrorPopup = false;
};

} // namespace editor
