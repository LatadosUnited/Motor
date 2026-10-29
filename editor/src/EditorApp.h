#pragma once

#include "EditorContext.h"
#include "EditorSettings.h"
#include "FolderWatcher.h"
#include "export/ExportController.h"
#include "play/GameView.h"
#include "play/PlayController.h"
#include "panels/ProjectPanel.h"
#include "document/SceneDocument.h"
#include "viewport/SceneViewport.h"
#include "scripts/ScriptController.h"
#include "UndoHistory.h"
#include "uidoc/UiDocEditor.h"
#include "uidoc/UiDocReload.h"
#include "mcp/McpController.h"

#include <motor/core/FrameStats.h>

#include <motor/Motor.h>

#include <filesystem>
#include <string>

namespace editor {

// Janela principal do editor: loop, layout dos painéis, barra de menus, atalhos e desfazer. O trabalho
// de cada área fica nas peças que ele monta: SceneViewport (viewport), PlayController (modo Play),
// SceneDocument (cena/projeto abertos e menu Arquivo) e ScriptController (scripts do projeto).
class EditorApp final : public motor::Application {
public:
    struct Options {
        std::filesystem::path project;  // pasta ou .mproject; vazio = reabre o último projeto
        int mcpPort = 0;                // > 0: liga o servidor MCP nesta porta (--mcp na linha de comando)
        std::filesystem::path createParent;  // --create <pasta-pai> <nome>: cria o projeto e abre (Fase 14)
        std::string createName;
        std::filesystem::path perfFile;  // --perf: contadores de cada quadro num CSV ao fechar (Fase 27)
        double seconds = 0.0;            // --segundos: > 0 fecha sozinho depois desse tempo
        int hubPort = 0;                 // --hub: aberto pelo Hub residente (motor/mcp/HubLink.h)
        std::string hubToken;            // do ambiente (MOTOR_MCP_TOKEN), com --hub
    };
    explicit EditorApp(Options options);

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
    void SelectAll();  // Ctrl+A
    bool IsInteracting() const;  // arrastando/editando algo: a alteração ainda não terminou
    void Undo();
    void Redo();
    void ApplyPlayRequest();
    void ApplyRequests();                                // desfazer/refazer e Play/Parar pedidos no frame anterior
    void ApplyPendingChanges(bool interactionFinished);  // exclusões, prefabs e o passo de desfazer do frame
    // Algo pode ter mudado a cena: o desfazer compara e os estáticos do renderer são refeitos (Fase 28).
    void NoteSceneActivity();
    void UpdateMinimized();                              // a parte do frame sem interface, com a janela minimizada
    void StopPlay();  // volta à edição (nada acontece se já estiver editando)
    void OnPlayStopped();
    void DrawProjectPanel();
    void DrawAboutPopup();  // Ajuda > Sobre
    void ReloadUiDocuments();  // .mui e .mstyle mudados: as entidades que os usam releem (também no Play)

    EditorContext m_ctx;
    EditorSettings m_settings;
    ProjectPanel m_projectPanel;
    UndoHistory m_history;
    SceneViewport m_viewport;
    GameView m_gameView;  // janela Jogo (Cena e Jogo lado a lado)
    PlayController m_play;
    SceneDocument m_document;  // depois dos membros que ele referencia
    ScriptController m_scripts;  // depois do documento e do Play, que ele referencia
    ExportController m_export;   // Arquivo > Exportar jogo; depois do documento e dos scripts
    UiDocEditor m_uiEditor;      // janela Editor de UI (Ver > Editor de UI, duplo clique num .mui)
    McpController m_mcp;         // depois de todas as peças que as ferramentas usam

    std::filesystem::path m_startupProject;
    std::filesystem::path m_createParent;
    std::string m_createName;
    int m_startupMcpPort = 0;
    std::filesystem::path m_perfFile;
    double m_quitAfterSeconds = 0.0;
    motor::PerfRecorder m_perf;  // --perf
    int m_historyRequest = 0;  // -1 desfazer, +1 refazer (executado no início do próximo frame)
    float m_assetRefreshTimer = 0.0f;
    FolderWatcher m_assetWatcher;  // a pasta do projeto: varre os arquivos só quando algo mudou
    UiDocReloader m_uiReloader;   // datas dos .mui e .mstyle usados na cena
    bool m_uiReloadNow = false;   // o Editor de UI gravou: confere já, sem esperar a varredura
    bool m_assetsChanged = true;   // mudança ainda não vista inteira (a importação de modelos espera o fim do Play)
    bool m_resetLayout = true;  // constrói o layout padrão se não houver um salvo
    bool m_showDemo = false;
    bool m_openAbout = false;
};

} // namespace editor
