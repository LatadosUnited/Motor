#pragma once

#include "EditorContext.h"
#include "GameExport.h"

#include <filesystem>
#include <string>

namespace editor {

class SceneDocument;
class ScriptController;

// Arquivo > Exportar jogo (Fase 10): janela com a cena inicial e a pasta de saída, conferência dos scripts e
// o resultado (abrir a pasta, rodar o jogo). A cópia dos arquivos é do GameExport (testável sem janela).
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

    // Sem janela (servidor MCP). `startupScene` relativo ao projeto (vazio = a do projeto, senão a cena aberta);
    // `outputDir` vazio = a pasta padrão (<projeto>/Exportado). `setStartup` grava a cena como inicial no projeto.
    GameExport::Result Export(const std::string& startupScene, const std::filesystem::path& outputDir, bool setStartup);

private:
    std::string DefaultStartupScene() const;  // a do projeto, senão a cena aberta

    Host m_host;
    bool m_openRequest = false;
    std::string m_scene;       // relativo ao projeto
    std::string m_outputDir;   // UTF-8
    bool m_setStartup = true;
    bool m_hasResult = false;
    GameExport::Result m_result;
};

} // namespace editor
