#pragma once

// Hub do Motor (Fase 14): o estado (configuração, versões instaladas, projetos), as ações (abrir, criar,
// adicionar, remover) e a janela (GLFW + ImGui). As ações são as mesmas para os botões e para a linha de
// comando (main.cpp); a lógica que dá para testar sem janela fica em HubCore.

#include "HubCore.h"
#include "HubPlatform.h"
#include "HubTray.h"

#include <Download.h>
#include <Manifest.h>

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct GLFWwindow;

namespace hub {

class HubApp {
public:
    // `editorArgs`: argumentos a mais para o editor em toda abertura (os que vêm depois de "--" na linha de
    // comando do hub, ex.: --no-settings --mcp para testes automáticos). `dataOverride`: a pasta do --data (ou a
    // do instalacao.json), vazia com a padrão; entra na linha do iniciar com o Windows.
    HubApp(platform::Paths paths, std::vector<std::string> editorArgs, fs::path dataOverride = {});

    void Reload();  // relê a configuração, as versões instaladas e os projetos

    const platform::Paths& GetPaths() const { return m_paths; }
    const HubConfig& Config() const { return m_config; }
    const std::vector<InstalledVersion>& Versions() const { return m_versions; }
    const std::vector<ProjectInfo>& Projects() const { return m_projects; }  // na ordem de Config().projects

    // Ações: true se deu certo; `message` diz o que aconteceu (ou o erro), em português.
    // `version` por valor: a ação relê a lista de versões (uma referência para ela ficaria inválida).
    bool OpenProject(const fs::path& root, InstalledVersion version, std::string& message);
    bool CreateProject(const fs::path& parent, const std::string& name, InstalledVersion version,
                       std::string& message);
    bool AddProjectFolder(const fs::path& folder, std::string& message);
    bool RemoveProject(const fs::path& root, std::string& message);  // só da lista: a pasta fica
    bool AddVersion(const fs::path& source, std::string& message);   // copia para Versões\<versão>
    bool RemoveVersion(const std::string& version, std::string& message);  // para a Lixeira
    bool SetDefaultVersion(const std::string& version, std::string& message);

    // Servidores de versões (Fase 15).
    struct RemoteServer {
        std::string url;
        std::optional<distrib::Manifest> manifest;
        std::string error;
    };
    static std::vector<RemoteServer> FetchServers(const std::vector<std::string>& urls);
    bool AddServer(const std::string& url, std::string& message);
    bool RemoveServer(const std::string& url, std::string& message);
    // Baixa e instala `version` ("mais-recente" = a mais nova dos servidores). `server` vazio = os do hub.json,
    // na ordem. Linha de comando; a janela baixa em segundo plano (StartDownload).
    bool DownloadVersion(const std::string& version, const std::string& server,
                         const std::function<void(const distrib::ReleaseProgress&)>& progress, std::string& message);

    // Iniciar com o Windows: o valor do Run no HKCU aponta para este hub e esta pasta de dados.
    bool StartsWithWindows() const;
    bool SetStartWithWindows(bool on, std::string& message);

    struct WindowOptions {
        int initialTab = 0;  // 0 Projetos, 1 Versões
        bool tray = false;   // --bandeja: abre escondido, só o ícone na bandeja
    };
    // Abre a janela e roda até fechar. Residente (com --bandeja ou iniciando com o Windows), fechar a janela só a
    // esconde e o hub sai pelo menu do ícone. Com outro hub da mesma pasta de dados aberto, mostra a janela dele
    // e sai.
    int RunWindow(const WindowOptions& options);

private:
    void SaveConfig();
    void RefreshProjects();
    void SetStatus(std::string message, bool error);

    // Interface
    void DrawFrame();
    void DrawProjectsTab();
    void DrawProjectCard(size_t index);
    void DrawVersionsTab();
    void DrawNewProjectPopup();
    void DrawOpenWithPopup();
    void DrawRemoveVersionPopup();
    void DrawAddingVersionPopup();
    void RequestOpen(size_t index, bool chooseVersion);  // abre direto ou pergunta a versão
    void StartAddVersion(const fs::path& source);
    void DrawRemoteSection();
    void DrawServersSection();
    void DrawHubUpdateNotice();
    void DrawStartupOption();
    void ShowWindow();
    std::string OwnStartupCommand() const;
    void StartFetch();
    void PollBackground();
    void StartDownload(const std::string& server, const distrib::Release& release);

    platform::Paths m_paths;
    std::vector<std::string> m_editorArgs;
    fs::path m_dataOverride;
    HubConfig m_config;
    std::vector<InstalledVersion> m_versions;
    std::vector<ProjectInfo> m_projects;

    GLFWwindow* m_window = nullptr;
    std::unique_ptr<platform::Tray> m_tray;  // só com a janela aberta
    bool m_startedInTray = false;
    bool m_startsWithWindows = false;  // lido ao abrir a janela e quando muda pela caixa
    bool m_toldAboutTray = false;      // balão "o Hub continua na bandeja", uma vez por execução
    double m_lastRefresh = 0.0;
    int m_selectTab = -1;  // aba a selecionar no próximo quadro (--aba)
    std::string m_search;
    std::string m_status;
    bool m_statusError = false;

    // Novo projeto
    bool m_openNewProject = false;
    std::string m_newName;
    std::string m_newParent;  // UTF-8
    std::string m_newVersion;

    // Abrir com outra versão
    bool m_openOpenWith = false;
    fs::path m_openWithRoot;
    std::string m_openWithVersion;

    // Remover versão
    bool m_openRemoveVersion = false;
    std::string m_removeVersion;

    // Adicionar versão (cópia em segundo plano: a pasta tem centenas de MB)
    struct AddResult {
        std::optional<InstalledVersion> version;
        std::string error;
    };
    std::future<AddResult> m_adding;
    fs::path m_addingSource;

    // Servidores e versões disponíveis (lidos em segundo plano: servidor fora do ar não trava a janela)
    std::vector<RemoteServer> m_remote;
    std::future<std::vector<RemoteServer>> m_fetching;
    bool m_fetchedOnce = false;
    std::string m_newServer;

    // Download de uma versão (uma por vez)
    struct DownloadJob {
        std::string server;
        distrib::Release release;
        std::atomic<bool> cancel{false};
        std::mutex mutex;
        distrib::ReleaseProgress progress;
        std::future<distrib::ReleaseOutcome> result;
    };
    std::unique_ptr<DownloadJob> m_download;
};

} // namespace hub
