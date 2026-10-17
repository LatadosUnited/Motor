#pragma once

// Janela do instalador do Hub (Fase 15): uma tela com a pasta, os atalhos, a versão do Motor (servidor ou pasta)
// e o aviso das ferramentas de compilação; depois o progresso e o fim. No modo --desinstalar, a confirmação
// (com a pergunta sobre as versões e o hub.json). GLFW + ImGui estáticos, como o hub.

#include "Setup.h"

#include <atomic>
#include <future>
#include <mutex>
#include <string>
#include <vector>

struct GLFWwindow;

namespace setup {

class SetupApp {
public:
    SetupApp(Options defaults, bool uninstall);
    int Run();  // até fechar a janela; 0 se instalou/desinstalou

private:
    enum class Page { Options, Working, Done };

    void DrawFrame();
    void DrawOptions();
    void DrawUninstallOptions();
    void DrawWorking();
    void DrawDone();
    void StartCheckServer();
    void StartWork();
    void RefreshTools();
    Options CurrentOptions() const;

    GLFWwindow* m_window = nullptr;
    bool m_uninstall = false;
    Page m_page = Page::Options;
    Options m_defaults;

    // Campos da tela (UTF-8)
    std::string m_installDir;
    std::string m_dataDir;
    bool m_startMenu = true;
    bool m_desktop = false;
    int m_motor = 0;  // 0 nenhum, 1 servidor, 2 pasta
    std::string m_server;
    std::string m_localVersion;
    bool m_removeData = false;
    std::string m_existing;  // "Hub 0.2.0 já instalado aqui"

    // Verificar o servidor (em segundo plano)
    struct Check {
        bool ok = false;
        std::string text;
    };
    std::future<Check> m_check;
    Check m_checkResult;
    std::string m_checkedServer;

    std::vector<distrib::BuildTool> m_tools;

    // Instalação em andamento
    std::future<bool> m_work;
    std::atomic<bool> m_cancel{false};
    std::mutex m_mutex;
    std::vector<std::string> m_log;
    distrib::ReleaseProgress m_progress;
    bool m_downloading = false;
    std::string m_error;
    bool m_success = false;
    bool m_openHub = true;
};

} // namespace setup
