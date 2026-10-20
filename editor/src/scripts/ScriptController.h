#pragma once

#include "EditorContext.h"
#include "scripts/ScriptBuilder.h"
#include "scripts/ScriptHost.h"
#include "scripts/ScriptProject.h"
#include "UndoHistory.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

class PlayController;
class SceneDocument;

// Scripts do projeto no editor: compilar a pasta Scripts/ (ao abrir o projeto, ao salvar um arquivo e pelo
// menu), mostrar a saída e os erros (painel e menu "Scripts"), recarregar a DLL sem fechar o editor (também
// durante o Play), criar scripts a partir de modelos e abrir os arquivos no editor de código (VS Code).
class ScriptController {
public:
    ScriptController(EditorContext& ctx, UndoHistory& history, SceneDocument& document, PlayController& play);

    void OpenProject();          // ao abrir um projeto: carrega a DLL já compilada e compila de novo
    void CloseProject();         // descarrega (troca de projeto, sair)
    void Update(float dt);       // vigia a pasta Scripts, acompanha a compilação e recarrega
    void RequestBuild();         // compila agora (ou logo que a compilação em andamento terminar)
    bool IsBuilding() const { return m_builder.IsRunning(); }
    void OnPlayStopped();        // (nada a fazer: a recarga não espera mais o Play parar)

    void OpenNewScriptPopup() { m_openNewScriptPopup = true; }  // "Novo script..." (também pelo painel Projeto)
    // Abre um arquivo do projeto no editor de código, na linha (0: só o arquivo).
    void OpenScript(const std::filesystem::path& file, int line = 0, int column = 0);
    // Cria Scripts/<name>.cpp a partir de um modelo. Para o MCP e o popup.
    bool CreateScript(const std::string& name, std::string_view templateId, std::filesystem::path* created,
                      std::string* error);

    // Situação dos scripts, para quem mostra fora do painel (servidor MCP).
    struct Status {
        std::string state;        // "sem projeto", "sem toolchain", "compilando", "ok", "falhou", "não compilado"
        float seconds = 0.0f;     // duração da última compilação (ou da atual)
        ScriptBuilder::Timings timings;
        float reloadSeconds = 0.0f;
        bool reloadPending = false;  // sempre false (a recarga acontece também no Play); mantido para o MCP
        std::string toolchainError;
        std::vector<std::string> loadedTypes;
        std::vector<ScriptProject::Diagnostic> diagnostics;
        std::string log;          // saída completa da última compilação
        uint64_t builds = 0;      // compilações terminadas desde que o projeto abriu (o MCP espera a próxima)
        bool queued = false;      // pedida durante outra: começa quando a atual terminar
    };
    Status GetStatus() const;

    void DrawMenu();             // menu "Scripts"
    void DrawStatus();           // aviso na barra de menus quando a compilação falha
    void DrawPanel();            // painel "Scripts"
    void DrawNewScriptPopup();

private:
    void Reload();               // carrega a DLL compilada (no Play, os scripts recomeçam com OnStart)
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
    ScriptProject::FolderWatch m_watch;  // aviso imediato de arquivo salvo; o timer fica de reserva
    float m_reloadSeconds = 0.0f;
    bool m_buildQueued = false;          // a pasta mudou durante a compilação: compila de novo ao terminar
    uint64_t m_builds = 0;               // compilações terminadas (Status::builds)
    std::vector<std::string> m_reloadWarnings;  // valores perdidos na última recarga
    std::string m_log;                   // cópia da saída da compilação para o painel
    std::vector<ScriptProject::Diagnostic> m_diagnostics;
    bool m_focusPanel = false;
    bool m_openNewScriptPopup = false;
    std::string m_newScriptName;
    std::string m_newScriptTemplate;
};

} // namespace editor
