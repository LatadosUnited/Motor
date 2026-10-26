// Hub: estado, ações (projetos, versões, servidores, downloads) e a janela. Abas em HubProjectsTab.cpp e
// HubVersionsTab.cpp; cores e botões em HubWidgets.cpp.

#include "HubApp.h"
#include "HubMcp.h"
#include "HubWidgets.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_stdlib.h>

#include <Platform.h>
#include <motor/mcp/HubLink.h>

#include <algorithm>
#include <cstdio>
#include <format>
#include <stdexcept>

#ifndef MOTOR_HUB_VERSION
#define MOTOR_HUB_VERSION "0.0.0"
#endif

namespace hub {

using namespace widgets;

namespace {

constexpr double kRefreshSeconds = 3.0;  // relê projetos e versões (mudam fora do hub: editor, Explorer)
} // namespace

HubApp::HubApp(platform::Paths paths, std::vector<std::string> editorArgs, fs::path dataOverride)
    : m_paths(std::move(paths)), m_editorArgs(std::move(editorArgs)) {
    if (!dataOverride.empty()) {
        std::error_code ec;
        m_dataOverride = fs::absolute(dataOverride, ec).lexically_normal();  // como o instalador grava
    }
    Reload();
}

HubApp::~HubApp() = default;  // aqui o HubMcp é um tipo completo

// ---------------------------------------------------------------------------
// Estado e ações
// ---------------------------------------------------------------------------

void HubApp::Reload() {
    std::string warning;
    m_config = HubConfig::Load(m_paths.configFile, &warning);
    if (!warning.empty()) SetStatus(warning, true);
    m_versions = ScanVersions(m_paths.versionsDir);
    RefreshProjects();
}

void HubApp::RefreshProjects() {
    m_versions = ScanVersions(m_paths.versionsDir);
    m_projects.clear();
    for (const HubProject& p : m_config.projects) m_projects.push_back(ReadProject(p.root));
}

void HubApp::SaveConfig() {
    std::string error;
    if (!m_config.Save(m_paths.configFile, &error)) SetStatus(error, true);
}

void HubApp::SetStatus(std::string message, bool error) {
    m_status = std::move(message);
    m_statusError = error;
}

bool HubApp::Launch(const fs::path& root, const InstalledVersion& version, const std::vector<std::string>& args,
                    std::string& message) {
    std::vector<std::string> all = args;
    // Hub residente: o editor liga o MCP numa porta interna, com um token só dele, e avisa o hub quando fica pronto.
    // Versões sem "hubLink" na lista de ferramentas não conhecem o --hub (sairiam com "opção desconhecida").
    int port = 0;
    std::string token;
    platform::ChildProcess::Environment env;
    const std::optional<EditorTools> tools = m_linkPort > 0 ? ReadEditorTools(version.dir) : std::nullopt;
    if (tools && tools->hubLink >= motor::mcp::hublink::kVersion) port = PickEditorPort();
    if (port > 0) {
        token = NewToken();
        all.push_back(std::format("--mcp={}", port));
        all.push_back(motor::mcp::hublink::kArgument);
        all.push_back(std::to_string(m_linkPort));
        env.emplace_back(motor::mcp::hublink::kTokenEnv, token);
    }
    all.insert(all.end(), m_editorArgs.begin(), m_editorArgs.end());
    auto process = platform::ChildProcess::Launch(version.editor, all, &message, env);
    if (!process) return false;
    m_editors.push_back({root, version.version, std::move(*process), NowSeconds(), port, std::move(token), false});
    return true;
}

int HubApp::PickEditorPort() {
    namespace link = motor::mcp::hublink;
    for (int port = link::kEditorPortFirst; port <= link::kEditorPortLast; ++port) {
        const bool used = std::any_of(m_editors.begin(), m_editors.end(), [&](const OpenEditor& e) {
            return e.mcpPort == port && e.process.Running();
        });
        if (!used && link::PortFree(port)) return port;
    }
    return 0;  // 99 editores abertos (ou portas tomadas): abre sem a ligação
}

void HubApp::PruneEditors() {
    std::erase_if(m_editors, [](const OpenEditor& e) { return !e.process.Running(); });
}

const std::vector<HubApp::OpenEditor>& HubApp::Editors() {
    PruneEditors();
    return m_editors;
}

const HubApp::OpenEditor* HubApp::FindEditor(const fs::path& root) {
    for (const OpenEditor& e : Editors()) {
        if (SamePath(e.root, root)) return &e;
    }
    return nullptr;
}

bool HubApp::CloseEditor(const fs::path& root, bool force, std::string& message) {
    const OpenEditor* editor = FindEditor(root);
    if (!editor) {
        message = std::format("Nenhum editor aberto pelo Hub em '{}'.", ToUtf8(root));
        return false;
    }
    if (force) {
        if (!editor->process.Terminate()) {
            message = "Não foi possível fechar o editor.";
            return false;
        }
        message = std::format("Editor de '{}' fechado à força (o que não estava salvo se perdeu).", ToUtf8(root.filename()));
        return true;
    }
    if (!editor->process.RequestClose()) {
        message = "O editor ainda não tem janela para receber o pedido de fechar (abrindo?).";
        return false;
    }
    message = std::format("Pedido para fechar o editor de '{}' enviado (ele pergunta se há algo não salvo).",
                          ToUtf8(root.filename()));
    return true;
}

const InstalledVersion* HubApp::PickVersion(const std::string& wanted, std::string& error) const {
    if (!wanted.empty()) {
        const InstalledVersion* v = FindVersion(m_versions, wanted);
        if (!v) error = std::format("O Motor {} não está instalado.", wanted);
        return v;
    }
    const InstalledVersion* v = DefaultVersion(m_versions, m_config.defaultVersion);
    if (!v) error = "Nenhuma versão do Motor instalada.";
    return v;
}

bool HubApp::OpenProjectAuto(const fs::path& root, const std::string& engine, std::string& message) {
    const ProjectInfo info = ReadProject(root);
    if (!info.found) {
        message = std::format("Projeto não encontrado em '{}': {}", ToUtf8(root), info.error);
        return false;
    }
    if (!engine.empty()) {
        const InstalledVersion* v = PickVersion(engine, message);
        return v && OpenProject(info.root, *v, message);
    }
    const VersionChoice choice = ChooseVersion(info.engineVersion, m_versions, m_config.defaultVersion);
    if (choice.match == VersionMatch::Exact) return OpenProject(info.root, *choice.version, message);
    if (choice.match == VersionMatch::NoVersions) {
        message = "Nenhuma versão do Motor instalada.";
        return false;
    }
    message = (info.engineVersion.empty() ? std::string("O projeto não diz a versão do Motor")
                                          : std::format("O Motor {} não está instalado", info.engineVersion)) +
              std::format(": escolha a versão (engine no MCP, --engine na linha de comando; sugerida: {}). O editor "
                          "vai atualizar o engineVersion do projeto; faça uma cópia de segurança antes.",
                          choice.version->version);
    return false;
}

bool HubApp::OpenProject(const fs::path& root, InstalledVersion version, std::string& message) {
    const ProjectInfo info = ReadProject(root);
    if (!info.found) {
        message = std::format("Projeto não encontrado em '{}': {}", ToUtf8(root), info.error);
        return false;
    }
    if (const OpenEditor* open = FindEditor(info.root)) {
        message = std::format("'{}' já está aberto no Motor {} (processo {}).", info.name, open->version,
                              open->process.Pid());
        return false;
    }
    if (!Launch(info.root, version, OpenProjectArgs(info.root), message)) return false;

    m_config.MarkOpened(info.root, NowSeconds());
    SaveConfig();
    RefreshProjects();
    message = std::format("Abrindo '{}' no Motor {}...", info.name, version.version);
    return true;
}

bool HubApp::CreateProject(const fs::path& parent, const std::string& name, InstalledVersion version,
                           std::string& message) {
    if (std::string problem = CheckNewProject(parent, name); !problem.empty()) {
        message = std::move(problem);
        return false;
    }
    if (!Launch(parent / PathFromUtf8(name), version, CreateProjectArgs(parent, name), message)) return false;

    // O editor cria a pasta logo ao abrir; a lista já mostra o projeto (como "não encontrado" até lá).
    m_config.MarkOpened(parent / PathFromUtf8(name), NowSeconds());
    m_config.newProjectDir = parent;
    SaveConfig();
    RefreshProjects();
    message = std::format("Criando '{}' no Motor {}...", name, version.version);
    return true;
}

bool HubApp::AddProjectFolder(const fs::path& folder, std::string& message) {
    const ProjectInfo info = ReadProject(folder);
    if (!info.found) {
        message = std::format("'{}' não é um projeto do Motor: {}", ToUtf8(folder), info.error);
        return false;
    }
    if (!m_config.AddProject(info.root)) {
        message = std::format("'{}' já está na lista.", info.name);
        return false;
    }
    SaveConfig();
    RefreshProjects();
    message = std::format("Projeto '{}' adicionado.", info.name);
    return true;
}

bool HubApp::RemoveProject(const fs::path& root, std::string& message) {
    if (!m_config.RemoveProject(root)) {
        message = "O projeto não está na lista.";
        return false;
    }
    SaveConfig();
    RefreshProjects();
    message = std::format("'{}' saiu da lista (a pasta continua no disco).", ToUtf8(root.filename()));
    return true;
}

bool HubApp::AddVersion(const fs::path& source, std::string& message) {
    auto installed = InstallVersion(source, m_paths.versionsDir, &message);
    if (!installed) return false;
    RefreshProjects();
    message = std::format("Motor {} instalado em '{}'.", installed->version, ToUtf8(installed->dir));
    return true;
}

bool HubApp::RemoveVersion(const std::string& version, std::string& message) {
    const InstalledVersion* v = FindVersion(m_versions, version);
    if (!v) {
        message = std::format("O Motor {} não está instalado.", version);
        return false;
    }
    if (!platform::MoveToTrash(v->dir, &message)) return false;
    RefreshProjects();
    message = std::format("Motor {} enviado para a Lixeira.", version);
    return true;
}

// ---------------------------------------------------------------------------
// Servidores de versões (Fase 15)
// ---------------------------------------------------------------------------

std::vector<HubApp::RemoteServer> HubApp::FetchServers(const std::vector<std::string>& urls) {
    std::vector<RemoteServer> out;
    for (const std::string& url : urls) {
        RemoteServer server;
        server.url = url;
        distrib::Client client(url);
        server.manifest = client.FetchManifest(&server.error);
        out.push_back(std::move(server));
    }
    return out;
}

bool HubApp::AddServer(const std::string& url, std::string& message) {
    std::string error;
    if (!distrib::ParseServerUrl(url, &error)) {
        message = error;
        return false;
    }
    const std::string normalized = distrib::NormalizeServerUrl(url);
    if (!m_config.AddServer(normalized)) {
        message = std::format("{} já está na lista.", normalized);
        return false;
    }
    SaveConfig();
    message = std::format("Servidor {} adicionado.", normalized);
    return true;
}

bool HubApp::RemoveServer(const std::string& url, std::string& message) {
    if (!m_config.RemoveServer(url)) {
        message = "O servidor não está na lista.";
        return false;
    }
    SaveConfig();
    message = std::format("Servidor {} saiu da lista.", url);
    return true;
}

bool HubApp::DownloadVersion(const std::string& version, const std::string& server,
                             const std::function<void(const distrib::ReleaseProgress&)>& progress,
                             std::string& message) {
    const std::vector<std::string> urls = server.empty() ? m_config.servers : std::vector<std::string>{server};
    if (urls.empty()) {
        message = "Nenhum servidor de versões configurado (use --servidor <URL>).";
        return false;
    }
    const bool latest = version == "mais-recente" || version == "latest";
    std::string chosenServer;
    std::optional<distrib::Release> chosen;
    std::string errors;
    for (const RemoteServer& s : FetchServers(urls)) {
        if (!s.manifest) {
            errors += "\n  " + s.error;
            continue;
        }
        const distrib::Release* r = latest ? s.manifest->Latest() : s.manifest->Find(version);
        if (r && (!chosen || (latest && CompareVersions(r->version, chosen->version) > 0))) {
            chosen = *r;
            chosenServer = s.url;
            if (!latest) break;  // o primeiro servidor da lista que tiver a versão
        }
    }
    if (!chosen) {
        message = (latest ? std::string("Nenhuma versão publicada nos servidores.")
                          : std::format("O Motor {} não está publicado nos servidores.", version)) +
                  errors;
        return false;
    }
    if (FindVersion(m_versions, chosen->version)) {
        message = std::format("O Motor {} já está instalado.", chosen->version);
        return false;
    }
    const distrib::ReleaseOutcome outcome =
        distrib::DownloadRelease(chosenServer, *chosen, m_paths.versionsDir, nullptr, progress);
    RefreshProjects();
    if (outcome.status != distrib::Status::Ok) {
        message = outcome.error;
        return false;
    }
    message = std::format("Motor {} baixado de {} ({} pela rede{}) e instalado em '{}'.", chosen->version,
                          chosenServer, distrib::FormatSize(outcome.received),
                          outcome.reused ? ", " + distrib::FormatSize(outcome.reused) + " retomados" : std::string(),
                          ToUtf8(outcome.dir));
    return true;
}

void HubApp::StartFetch() {
    if (m_fetching.valid()) return;
    m_fetching = std::async(std::launch::async, [urls = m_config.servers] {
        auto result = FetchServers(urls);
        glfwPostEmptyEvent();  // acorda o loop da janela
        return result;
    });
}

void HubApp::StartDownload(const std::string& server, const distrib::Release& release) {
    if (m_download) return;
    m_download = std::make_unique<DownloadJob>();
    DownloadJob* job = m_download.get();
    job->server = server;
    job->release = release;
    job->progress.total = release.size;
    job->progress.filesTotal = release.files.size();
    job->result = std::async(std::launch::async, [job, versionsDir = m_paths.versionsDir] {
        auto outcome = distrib::DownloadRelease(job->server, job->release, versionsDir, &job->cancel,
                                                [job](const distrib::ReleaseProgress& p) {
                                                    std::lock_guard lock(job->mutex);
                                                    job->progress = p;
                                                });
        glfwPostEmptyEvent();
        return outcome;
    });
    SetStatus(std::format("Baixando o Motor {} de {}...", release.version, server), false);
}

void HubApp::PollBackground() {
    if (m_fetching.valid() && m_fetching.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_remote = m_fetching.get();
        m_fetchedOnce = true;
    }
    if (m_download && m_download->result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const distrib::ReleaseOutcome outcome = m_download->result.get();
        const std::string version = m_download->release.version;
        m_download.reset();
        RefreshProjects();
        if (outcome.status == distrib::Status::Ok) {
            SetStatus(std::format("Motor {} baixado e instalado em '{}' ({} conferidos pelo SHA-256).", version,
                                  ToUtf8(outcome.dir), distrib::FormatSize(outcome.received + outcome.reused)),
                      false);
        } else {
            SetStatus(std::format("Motor {}: {}", version, outcome.error), outcome.status == distrib::Status::Failed);
        }
        m_lastDownload = DownloadResult{version, outcome.status == distrib::Status::Ok, m_status};
    }
    PruneEditors();
}

bool HubApp::SetDefaultVersion(const std::string& version, std::string& message) {
    if (!FindVersion(m_versions, version)) {
        message = std::format("O Motor {} não está instalado.", version);
        return false;
    }
    m_config.defaultVersion = version;
    SaveConfig();
    message = std::format("Motor {} é a versão padrão.", version);
    return true;
}

// ---------------------------------------------------------------------------
// Iniciar com o Windows
// ---------------------------------------------------------------------------

std::string HubApp::OwnStartupCommand() const {
    return distrib::StartupCommand(distrib::platform::ExecutablePath(), m_dataOverride);
}

bool HubApp::StartsWithWindows() const {
    const auto current = distrib::platform::ReadStartupValue(distrib::kStartupValue);
    return current && SameStartupCommand(*current, OwnStartupCommand());
}

bool HubApp::SetStartWithWindows(bool on, std::string& message) {
    if (on) {
        if (!distrib::platform::WriteStartupValue(distrib::kStartupValue, OwnStartupCommand(), &message)) return false;
        message = "O Hub vai iniciar com o Windows, na bandeja.";
    } else {
        if (!distrib::platform::DeleteStartupValue(distrib::kStartupValue)) {
            message = "Não foi possível tirar o Hub do início do Windows.";
            return false;
        }
        message = "O Hub não inicia mais com o Windows.";
    }
    m_startsWithWindows = on;
    // Ligado agora, este hub já fica residente; desligado, só continua na bandeja se abriu nela.
    if (m_tray) {
        if (on || m_startedInTray) {
            m_tray->ShowIcon(std::format("Hub do Motor {}", MOTOR_HUB_VERSION));
        } else {
            m_tray->HideIcon();
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Janela
// ---------------------------------------------------------------------------

void HubApp::ShowWindow() {
    if (glfwGetWindowAttrib(m_window, GLFW_ICONIFIED)) glfwRestoreWindow(m_window);
    glfwShowWindow(m_window);
    glfwFocusWindow(m_window);
    m_lastRefresh = 0.0;  // a lista pode ter mudado enquanto estava escondida
}

int HubApp::RunWindow(const WindowOptions& options) {
    m_selectTab = options.initialTab;
    m_startedInTray = options.tray;
    m_tray = std::make_unique<platform::Tray>(InstanceKey(m_paths.dataDir));
    // Já há um hub desta pasta de dados: o atalho traz a janela dele; o --bandeja do Windows não faz nada.
    if (!m_tray->Claim(!options.tray)) {
        m_tray.reset();
        return 0;
    }
    m_startsWithWindows = StartsWithWindows();
    if (options.tray || m_startsWithWindows) m_tray->ShowIcon(std::format("Hub do Motor {}", MOTOR_HUB_VERSION));

    if (!glfwInit()) throw std::runtime_error("Não foi possível iniciar o GLFW.");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, options.tray ? GLFW_FALSE : GLFW_TRUE);
    const std::string title = std::format("Hub do Motor {}", MOTOR_HUB_VERSION);
    m_window = glfwCreateWindow(1040, 680, title.c_str(), nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("Não foi possível criar a janela (OpenGL 3.3).");
    }
    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // nada de imgui.ini na pasta de trabalho
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ApplyStyle();
    LoadFont();
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    m_lastRefresh = glfwGetTime();
    StartFetch();
    if (options.mcpPort > 0) {
        m_mcp = std::make_unique<HubMcp>(*this);
        // glfwPostEmptyEvent pode ser chamado de outra thread: acorda o glfwWaitEvents do loop.
        if (!m_mcp->Start(options.mcpPort, [] { glfwPostEmptyEvent(); }, m_mcpError)) SetStatus(m_mcpError, true);
    }
    while (true) {
        // O hub fica parado quase sempre: espera eventos em vez de girar o loop. Escondido, só o ícone e os pedidos
        // de outro hub o acordam (são mensagens da janela da bandeja, atendidas aqui).
        const bool busy = m_adding.valid() || m_download || m_fetching.valid() || (m_mcp && m_mcp->HasParked());
        const bool visible = glfwGetWindowAttrib(m_window, GLFW_VISIBLE) != 0;
        glfwWaitEventsTimeout(busy ? 0.05 : visible ? 0.5 : 2.0);
        if (m_tray->TakeQuitRequest()) break;
        if (m_tray->TakeShowRequest()) ShowWindow();
        if (glfwWindowShouldClose(m_window)) {
            if (!m_tray->IconVisible()) break;
            // Residente: fechar a janela só a esconde.
            glfwSetWindowShouldClose(m_window, GLFW_FALSE);
            glfwHideWindow(m_window);
            if (!m_toldAboutTray) {
                m_toldAboutTray = true;
                m_tray->Notify("O Hub continua na bandeja",
                               "Clique no ícone para abrir. Para fechar de vez, use \"Sair do Hub\" no menu do ícone.");
            }
        }
        PollBackground();
        if (m_mcp) m_mcp->Pump();  // também escondido: o MCP é o que o hub residente faz
        if (!glfwGetWindowAttrib(m_window, GLFW_VISIBLE) || glfwGetWindowAttrib(m_window, GLFW_ICONIFIED)) continue;
        if (glfwGetTime() - m_lastRefresh > kRefreshSeconds) {
            m_lastRefresh = glfwGetTime();
            RefreshProjects();
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        DrawFrame();
        ImGui::Render();

        int width = 0, height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.11f, 0.12f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(m_window);
    }

    if (m_adding.valid()) m_adding.wait();  // não abandona uma cópia pela metade
    if (m_download) {
        m_download->cancel = true;  // o download parcial fica para retomar depois
        m_download->result.wait();
    }
    if (m_fetching.valid()) m_fetching.wait();
    m_mcp.reset();  // os pedidos esperando recebem "ocupado"
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(m_window);
    glfwTerminate();
    m_tray.reset();  // tira o ícone e solta a trava
    return 0;
}

void HubApp::DrawFrame() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Hub", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::PushFont(nullptr, 24.0f);
    ImGui::TextUnformatted("Hub do Motor");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kDim, "%s", MOTOR_HUB_VERSION);
    DrawHubUpdateNotice();
    DrawStartupOption();

    if (ImGui::BeginTabBar("Abas")) {
        const auto flags = [&](int tab) { return m_selectTab == tab ? ImGuiTabItemFlags_SetSelected : 0; };
        if (ImGui::BeginTabItem("Projetos", nullptr, flags(0))) {
            DrawProjectsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Versões", nullptr, flags(1))) {
            DrawVersionsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("MCP", nullptr, flags(2))) {
            DrawMcpTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        m_selectTab = -1;
    }

    DrawNewProjectPopup();
    DrawOpenWithPopup();
    DrawRemoveVersionPopup();
    DrawAddingVersionPopup();
    ImGui::End();
}

void HubApp::DrawStartupOption() {
    // No canto direito do cabeçalho.
    constexpr const char* kLabel = "Iniciar com o Windows";
    const ImGuiStyle& style = ImGui::GetStyle();
    const float width = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(kLabel).x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - width));
    bool on = m_startsWithWindows;
    if (ImGui::Checkbox(kLabel, &on)) {
        std::string message;
        const bool ok = SetStartWithWindows(on, message);
        SetStatus(message, !ok);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Abre o Hub escondido, na bandeja, ao entrar no Windows.\n"
                          "Com ele residente, fechar a janela só a esconde.");
    }
}

} // namespace hub
