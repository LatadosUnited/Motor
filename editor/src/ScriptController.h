#pragma once

#include "EditorContext.h"
#include "ScriptBuilder.h"
#include "ScriptHost.h"
#include "ScriptProject.h"
#include "UndoHistory.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace editor {

class PlayController;
class SceneDocument;

// Scripts do projeto no editor: compilar a pasta Scripts/ (ao abrir o projeto, ao salvar um arquivo e pelo
// menu), mostrar a saída e os erros (painel e menu "Scripts") e recarregar a DLL sem fechar o editor
// (adiado até parar o Play).
class ScriptController {
public:
    ScriptController(EditorContext& ctx, UndoHistory& history, SceneDocument& document, PlayController& play);

    void OpenProject();          // ao abrir um projeto: carrega a DLL já compilada e compila de novo
    void CloseProject();         // descarrega (troca de projeto, sair)
    void Update(float dt);       // vigia a pasta Scripts, acompanha a compilação e recarrega
    void RequestBuild();         // compila agora (ou logo que a compilação em andamento terminar)
    void OnPlayStopped();        // recarrega se recompilou durante o Play

    // Situação dos scripts, para quem mostra fora do painel (servidor MCP).
    struct Status {
        std::string state;        // "sem projeto", "sem toolchain", "compilando", "ok", "falhou", "não compilado"
        float seconds = 0.0f;     // duração da última compilação (ou da atual)
        bool reloadPending = false;
        std::string toolchainError;
        std::vector<std::string> loadedTypes;
        std::vector<ScriptProject::Diagnostic> diagnostics;
        std::string log;          // saída completa da última compilação
    };
    Status GetStatus() const;

    void DrawMenu();             // menu "Scripts"
    void DrawStatus();           // aviso na barra de menus quando a compilação falha
    void DrawPanel();            // painel "Scripts"
    void DrawNewScriptPopup();

private:
    void Reload();               // carrega a DLL compilada; só fora do Play
    ScriptHost::ForEachScene AllScenes();  // cena aberta e fotos do desfazer

    EditorContext& m_ctx;
    UndoHistory& m_history;
    SceneDocument& m_document;
    PlayController& m_play;

    ScriptBuilder m_builder;
    ScriptHost m_host;
    std::optional<ScriptProject::Toolchain> m_toolchain;
    std::string m_toolchainError;
    std::string m_signature;             // estado da pasta Scripts na última verificação
    std::filesystem::file_time_type m_loadedDllTime{};  // data da DLL carregada (recompilou sem mudar = não recarrega)
    float m_watchTimer = 0.0f;
    bool m_buildQueued = false;          // a pasta mudou durante a compilação: compila de novo ao terminar
    bool m_reloadPending = false;        // compilou durante o Play: recarrega ao parar
    std::string m_log;                   // cópia da saída da compilação para o painel
    std::vector<ScriptProject::Diagnostic> m_diagnostics;
    bool m_focusPanel = false;
    bool m_openNewScriptPopup = false;
    std::string m_newScriptName;
};

} // namespace editor
