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

class HubMcp;

class HubApp {
public:
    // `editorArgs`: argumentos a mais para o editor em toda abertura (os que vêm depois de "--" na linha de
    // comando do hub, ex.: --no-settings --mcp para testes automáticos). `dataOverride`: a pasta do --data (ou a
    // do instalacao.json), vazia com a padrão; entra na linha do iniciar com o Windows.
    HubApp(platform::Paths paths, std::vector<std::string> editorArgs, fs::path dataOverride = {});
    ~HubApp();

    void Reload();  // relê a configuração, as versões instaladas e os projetos

    const platform::Paths& GetPaths() const { return m_paths; }
    const HubConfig& Config() const { return m_config; }
    const std::vector<InstalledVersion>& Versions() const { return m_versions; }
    const std::vector<ProjectInfo>& Projects() const { return m_projects; }  // na ordem de Config().projects

    // Ações: true se deu certo; `message` diz o que aconteceu (ou o erro), em português.
    // `version` por valor: a ação relê a lista de versões (uma referência para ela ficaria inválida).
    // Um projeto com o editor aberto por este hub não abre de novo.
    bool OpenProject(const fs::path& root, InstalledVersion version, std::string& message);
    // Abre com a versão do projeto ou, com `engine`, com essa. Sem a versão do projeto instalada, falha dizendo qual
    // usar com `engine` (o editor passa o projeto para ela: cópia de segurança antes).
    bool OpenProjectAuto(const fs::path& root, const std::string& engine, std::string& message);
    // `wanted` vazio = a versão padrão. nullptr e `error` se não estiver instalada.
    const InstalledVersion* PickVersion(const std::string& wanted, std::string& error) const;
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

    // Editores abertos por este hub (os abertos de outro jeito não entram), sem os que já fecharam.
    struct OpenEditor {
        fs::path root;  // muda se o editor abrir outro projeto (aviso "projeto")
        std::string version;
        platform::ChildProcess process;
        int64_t opened = 0;  // NowSeconds()
        // Hub residente (motor/mcp/HubLink.h): só com o MCP do hub ligado e uma versão que entende o --hub.
        int mcpPort = 0;     // porta interna do MCP do editor; 0 = aberto sem a ligação
        std::string token;   // o do MCP do editor (e dos avisos dele)
        bool ready = false;  // avisou "pronto" (e ainda não "fechando")
    };
    const std::vector<OpenEditor>& Editors();
    const OpenEditor* FindEditor(const fs::path& root);
    // Pede para o editor do projeto fechar (pergunta se há algo não salvo) ou, com `force`, fecha na hora.
    bool CloseEditor(const fs::path& root, bool force, std::string& message);

    // Iniciar com o Windows: o valor do Run no HKCU aponta para este hub e esta pasta de dados.
    bool StartsWithWindows() const;
    bool SetStartWithWindows(bool on, std::string& message);

    // Autoatualização (Tarefa 9 do Hub residente): o Hub mais novo publicado com o instalador (MotorSetup.exe).
    struct HubUpdate {
        std::string version;
        std::string server;
        distrib::ReleaseFile setup;  // tamanho e SHA-256 do MotorSetup.exe no manifesto
    };
    // O mais novo que este hub entre os servidores lidos (nullopt se não houver).
    static std::optional<HubUpdate> NewestHub(const std::vector<RemoteServer>& servers);
    std::optional<HubUpdate> AvailableHubUpdate() const { return NewestHub(m_remote); }
    // Só um hub instalado pelo MotorSetup (instalacao.json ao lado do .exe) se atualiza sozinho. false e `why`
    // dizendo por quê (pasta de build, outro download ou cópia em andamento).
    bool CanSelfUpdate(std::string* why = nullptr) const;
    // Baixa o instalador em segundo plano para <dados>\Atualizacao (SHA-256 conferido, retoma um download
    // interrompido) e, pronto, o abre em silêncio e sai: o instalador troca o MotorHub.exe e abre o hub novo na
    // bandeja com os mesmos argumentos. Os editores abertos continuam e o hub novo volta a acompanhá-los.
    bool StartHubUpdate(const HubUpdate& update, std::string& message);
    bool HubUpdateRunning() const { return m_hubUpdate != nullptr || m_quitForUpdate; }

    struct WindowOptions {
        int initialTab = 0;  // 0 Projetos, 1 Versões
        bool tray = false;   // --bandeja: abre escondido, só o ícone na bandeja
        int mcpPort = kHubMcpPort;  // --mcp-porta; 0 = MCP do hub desligado
        std::vector<std::string> relaunchArgs;  // a linha deste hub: o hub novo abre com ela depois de atualizar
    };
    // Abre a janela e roda até fechar. Residente (com --bandeja ou iniciando com o Windows), fechar a janela só a
    // esconde e o hub sai pelo menu do ícone. Com outro hub da mesma pasta de dados aberto, mostra a janela dele
    // e sai.
    int RunWindow(const WindowOptions& options);

private:
    friend class HubMcp;  // as ferramentas do MCP usam as mesmas ações e downloads da janela

    void SaveConfig();
    void PruneEditors();
    bool Launch(const fs::path& root, const InstalledVersion& version, const std::vector<std::string>& args,
                std::string& message);
    void RefreshProjects();
    void SetStatus(std::string message, bool error);

    // Interface
    void DrawFrame();
    void DrawProjectsTab();
    void DrawProjectCard(size_t index);
    void DrawVersionsTab();
    void DrawMcpTab();
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
    // Autoatualização (HubUpdate.cpp)
    void PollHubUpdate();
    void SaveOpenEditors();    // ao sair: editores.json para o próximo hub
    void AdoptSavedEditors();  // ao abrir: volta a acompanhar os editores do hub anterior
    void ReadUpdateLog();      // ao abrir: diz como foi a atualização e limpa os instaladores velhos

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
    std::unique_ptr<HubMcp> m_mcp;     // só com a janela aberta
    std::string m_mcpError;            // por que o MCP do hub não ligou
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
    struct DownloadResult {
        std::string version;
        bool ok = false;
        std::string message;
    };
    std::optional<DownloadResult> m_lastDownload;  // o último que terminou (o MCP espera por ele)

    // Autoatualização do Hub: download do instalador (um por vez) e a saída logo depois de abri-lo.
    struct HubUpdateJob {
        HubUpdate update;
        fs::path dest;
        std::atomic<bool> cancel{false};
        std::atomic<uint64_t> bytes{0};
        std::future<distrib::Client::FileResult> result;
    };
    std::unique_ptr<HubUpdateJob> m_hubUpdate;
    std::string m_hubUpdateMessage;  // o último resultado (o MCP e a janela mostram)
    bool m_hubUpdateOk = false;
    bool m_quitForUpdate = false;    // instalador aberto: sai daqui a pouco (a resposta do MCP sai antes)
    double m_quitAt = 0.0;
    std::vector<std::string> m_relaunchArgs;

    std::vector<OpenEditor> m_editors;
    int m_linkPort = 0;  // porta do MCP do hub enquanto ligado (HubMcp): os editores abertos avisam nela
    int PickEditorPort();  // livre na faixa interna e sem outro editor deste hub; 0 se não houver
};

} // namespace hub
