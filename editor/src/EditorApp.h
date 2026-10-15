#pragma once

#include "EditorContext.h"
#include "EditorSettings.h"
#include "ProjectPanel.h"
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

namespace editor {

class EditorApp final : public motor::Application {
public:
    // projectToOpen: pasta ou .mproject passado na linha de comando; vazio = reabre o último projeto.
    explicit EditorApp(std::filesystem::path projectToOpen = {});

private:
    void OnInit() override;
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
    bool m_openNewProjectPopup = false;
    std::string m_newProjectName;
    std::string m_newProjectLocation;  // UTF-8
    std::string m_errorMessage;
    bool m_openErrorPopup = false;
};

} // namespace editor
