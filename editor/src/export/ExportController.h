#pragma once

#include "EditorContext.h"
#include "export/GameExport.h"
#include "scripts/ScriptBuilder.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace editor {

class SceneDocument;
class ScriptController;

// Arquivo > Exportar jogo (Fase 10): janela com a cena inicial e a pasta de saída, conferência dos scripts e
// o resultado (abrir a pasta, rodar o jogo). A cópia dos arquivos é do GameExport (testável sem janela).
// Com um Motor em Release (ReleaseRuntime), a DLL de scripts é recompilada em Release antes da cópia, em segundo
// plano (a janela mostra o andamento).
class ExportController {
public:
    struct Host {
        EditorContext& ctx;
        SceneDocument& document;
        ScriptController& scripts;
    };

    explicit ExportController(Host host) : m_host(host) {}

    void Open();        // abre a janela (item do menu Arquivo)
    void DrawPopup();

    // Começa a exportação (false e `failure` se nem deu para começar); Poll devolve true quando terminou, com o
    // resultado. Enquanto IsBusy, os scripts compilam em Release.
    bool Begin(const std::string& startupScene, const std::filesystem::path& outputDir, bool setStartup,
               GameExport::Result& failure);
    bool Poll(GameExport::Result& result);
    bool IsBusy() const { return m_pending.has_value(); }

    // Sem janela (servidor MCP): Begin e espera. `startupScene` relativo ao projeto (vazio = a do projeto, senão a cena aberta);
    // `outputDir` vazio = a pasta padrão (<projeto>/Exportado). `setStartup` grava a cena como inicial no projeto.
    GameExport::Result Export(const std::string& startupScene, const std::filesystem::path& outputDir, bool setStartup);

private:
    struct Pending {
        GameExport::Options options;
        bool setStartup = true;
        bool building = false;  // compilando os scripts em Release
        std::vector<std::string> warnings;
    };

    std::string DefaultStartupScene() const;  // a do projeto, senão a cena aberta
    GameExport::Result Finish(const Pending& pending);

    Host m_host;
    bool m_openRequest = false;
    std::string m_scene;       // relativo ao projeto
    std::string m_outputDir;   // UTF-8
    bool m_setStartup = true;
    bool m_hasResult = false;
    GameExport::Result m_result;
    std::optional<Pending> m_pending;
    ScriptBuilder m_releaseBuilder;
};

} // namespace editor
