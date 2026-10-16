#pragma once

#include "EditorContext.h"
#include "EditorSettings.h"
#include "PlayController.h"
#include "ProjectPanel.h"
#include "SceneDocument.h"
#include "SceneViewport.h"
#include "ScriptController.h"
#include "UndoHistory.h"

#include <motor/Motor.h>

#include <filesystem>

namespace editor {

// Janela principal do editor: loop, layout dos painéis, barra de menus, atalhos e desfazer. O trabalho
// de cada área fica nas peças que ele monta: SceneViewport (viewport), PlayController (modo Play),
// SceneDocument (cena/projeto abertos e menu Arquivo) e ScriptController (scripts do projeto).
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
    void BuildDockLayout(unsigned int dockspaceId);
    void HandleShortcuts();
    bool IsInteracting() const;  // arrastando/editando algo: a alteração ainda não terminou
    void Undo();
    void Redo();
    void ApplyPlayRequest();
    void StopPlay();  // volta à edição (nada acontece se já estiver editando)
    void OnPlayStopped();
    void DrawProjectPanel();

    EditorContext m_ctx;
    EditorSettings m_settings;
    ProjectPanel m_projectPanel;
    UndoHistory m_history;
    SceneViewport m_viewport;
    PlayController m_play;
    SceneDocument m_document;  // depois dos membros que ele referencia
    ScriptController m_scripts;  // depois do documento e do Play, que ele referencia

    std::filesystem::path m_startupProject;
    int m_historyRequest = 0;  // -1 desfazer, +1 refazer (executado no início do próximo frame)
    float m_assetRefreshTimer = 0.0f;
    bool m_resetLayout = true;  // constrói o layout padrão se não houver um salvo
    bool m_showDemo = false;
};

} // namespace editor
