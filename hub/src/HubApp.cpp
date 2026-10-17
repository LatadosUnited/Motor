#include "HubApp.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_stdlib.h>

#include <Platform.h>

#include <algorithm>
#include <cstdio>
#include <format>
#include <stdexcept>

#ifndef MOTOR_HUB_VERSION
#define MOTOR_HUB_VERSION "0.0.0"
#endif

namespace hub {

namespace {
constexpr double kRefreshSeconds = 3.0;  // relê projetos e versões (mudam fora do hub: editor, Explorer)

const ImVec4 kGreen(0.45f, 0.85f, 0.55f, 1.0f);
const ImVec4 kOrange(0.95f, 0.65f, 0.25f, 1.0f);
const ImVec4 kRed(0.95f, 0.40f, 0.35f, 1.0f);
const ImVec4 kDim(0.60f, 0.62f, 0.66f, 1.0f);

void ApplyStyle() {
    // As mesmas cores do editor (engine/src/core/ImGuiLayer.cpp), sem depender da engine.
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 4.0f;
    s.FrameRounding = 3.0f;
    s.ChildRounding = 4.0f;
    s.GrabRounding = 3.0f;
    s.TabRounding = 3.0f;
    s.FramePadding = ImVec2(8.0f, 5.0f);
    s.ItemSpacing = ImVec2(8.0f, 6.0f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.12f, 0.13f, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.14f, 0.15f, 0.17f, 1.00f);
    c[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.10f, 0.11f, 0.98f);
    c[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.18f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.24f, 0.27f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.29f, 0.33f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.20f, 0.33f, 0.52f, 0.80f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.40f, 0.62f, 0.80f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
    c[ImGuiCol_Tab] = ImVec4(0.13f, 0.14f, 0.16f, 1.00f);
    c[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.40f, 0.62f, 1.00f);
    c[ImGuiCol_TabSelected] = ImVec4(0.19f, 0.27f, 0.39f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.25f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.40f, 0.62f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
}

void LoadFont() {
    ImGuiIO& io = ImGui::GetIO();
#ifdef _WIN32
    for (const char* path : {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"}) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && io.Fonts->AddFontFromFileTTF(path, 17.0f)) return;
    }
#endif
    io.Fonts->AddFontDefault();
}

bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.42f, 0.70f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.50f, 0.80f, 1.0f));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(2);
    return pressed;
}

// Combo das versões instaladas; `selected` é o texto da versão.
void VersionCombo(const char* label, const std::vector<InstalledVersion>& versions, std::string& selected,
                  std::string_view defaultVersion) {
    const std::string preview = selected.empty() ? "(nenhuma)" : "Motor " + selected;
    if (!ImGui::BeginCombo(label, preview.c_str())) return;
    for (const InstalledVersion& v : versions) {
        const std::string item = std::format("Motor {}{}", v.version, v.version == defaultVersion ? " (padrão)" : "");
        if (ImGui::Selectable(item.c_str(), v.version == selected)) selected = v.version;
    }
    ImGui::EndCombo();
}
} // namespace

HubApp::HubApp(platform::Paths paths, std::vector<std::string> editorArgs)
    : m_paths(std::move(paths)), m_editorArgs(std::move(editorArgs)) {
    Reload();
}

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

bool HubApp::OpenProject(const fs::path& root, InstalledVersion version, std::string& message) {
    const ProjectInfo info = ReadProject(root);
    if (!info.found) {
        message = std::format("Projeto não encontrado em '{}': {}", ToUtf8(root), info.error);
        return false;
    }
    std::vector<std::string> args = OpenProjectArgs(info.root);
    args.insert(args.end(), m_editorArgs.begin(), m_editorArgs.end());
    if (!platform::LaunchProcess(version.editor, args, &message)) return false;

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
    std::vector<std::string> args = CreateProjectArgs(parent, name);
    args.insert(args.end(), m_editorArgs.begin(), m_editorArgs.end());
    if (!platform::LaunchProcess(version.editor, args, &message)) return false;

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
    }
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
// Janela
// ---------------------------------------------------------------------------

int HubApp::RunWindow(int initialTab) {
    m_selectTab = initialTab;
    if (!glfwInit()) throw std::runtime_error("Não foi possível iniciar o GLFW.");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
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
    while (!glfwWindowShouldClose(m_window)) {
        // O hub fica parado quase sempre: espera eventos em vez de girar o loop.
        const bool busy = m_adding.valid() || m_download || m_fetching.valid();
        glfwWaitEventsTimeout(busy ? 0.05 : 0.5);
        PollBackground();
        if (glfwGetWindowAttrib(m_window, GLFW_ICONIFIED)) continue;
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
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(m_window);
    glfwTerminate();
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
        ImGui::EndTabBar();
        m_selectTab = -1;
    }

    DrawNewProjectPopup();
    DrawOpenWithPopup();
    DrawRemoveVersionPopup();
    DrawAddingVersionPopup();
    ImGui::End();
}

// Barra de situação no rodapé da aba (última ação).
static void DrawStatusLine(const std::string& status, bool error) {
    if (status.empty()) return;
    ImGui::Separator();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(error ? kRed : kDim, "%s", status.c_str());
    ImGui::PopTextWrapPos();
}

void HubApp::DrawProjectsTab() {
    if (PrimaryButton("Novo projeto")) {
        m_openNewProject = true;
        m_newName = "Novo Jogo";
        const fs::path parent = m_config.newProjectDir.empty() ? m_paths.defaultProjectsDir : m_config.newProjectDir;
        m_newParent = ToUtf8(parent);
        const InstalledVersion* def = DefaultVersion(m_versions, m_config.defaultVersion);
        m_newVersion = def ? def->version : std::string();
    }
    ImGui::SameLine();
    if (ImGui::Button("Adicionar pasta...")) {
        if (auto folder = platform::PickFolder(m_paths.defaultProjectsDir, L"Pasta de um projeto do Motor")) {
            std::string message;
            const bool ok = AddProjectFolder(*folder, message);
            SetStatus(message, !ok);
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##busca", "Buscar por nome ou pasta", &m_search);
    ImGui::Spacing();

    const float statusHeight = m_status.empty() ? 0.0f : ImGui::GetFrameHeightWithSpacing() * 1.6f;
    ImGui::BeginChild("ListaProjetos", ImVec2(0, -statusHeight), ImGuiChildFlags_None);
    // Abertos mais recentemente primeiro; os nunca abertos pelo hub depois, na ordem da lista.
    std::vector<size_t> order(m_projects.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const int64_t ta = a < m_config.projects.size() ? m_config.projects[a].lastOpened : 0;
        const int64_t tb = b < m_config.projects.size() ? m_config.projects[b].lastOpened : 0;
        return ta > tb;
    });
    size_t shown = 0;
    for (const size_t i : order) {
        if (i >= m_projects.size()) break;  // um cartão removeu um projeto da lista neste quadro
        const ProjectInfo& p = m_projects[i];
        if (!MatchesSearch(p.name, m_search) && !MatchesSearch(ToUtf8(p.root), m_search)) continue;
        DrawProjectCard(i);
        ++shown;
    }
    if (m_projects.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(kDim, "Nenhum projeto ainda. Crie um em \"Novo projeto\" ou adicione a pasta de um existente.");
    } else if (shown == 0) {
        ImGui::TextColored(kDim, "Nenhum projeto com \"%s\".", m_search.c_str());
    }
    if (m_versions.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(kOrange, "Nenhuma versão do Motor instalada: adicione uma na aba Versões.");
    }
    ImGui::EndChild();
    DrawStatusLine(m_status, m_statusError);
}

void HubApp::DrawProjectCard(size_t index) {
    const ProjectInfo p = m_projects[index];  // cópia: as ações abaixo podem recarregar a lista
    const HubProject* entry = index < m_config.projects.size() ? &m_config.projects[index] : nullptr;
    const int64_t lastOpened = entry ? entry->lastOpened : 0;

    ImGui::PushID(static_cast<int>(index));
    ImGui::BeginChild("Cartao", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    const float buttonsWidth = 190.0f;
    const float startX = ImGui::GetCursorPosX();
    const float innerWidth = ImGui::GetContentRegionAvail().x;
    const float textWidth = innerWidth - buttonsWidth;
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, 20.0f);
    ImGui::TextUnformatted(p.name.c_str());
    ImGui::PopFont();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWidth);
    ImGui::TextColored(kDim, "%s", ToUtf8(p.root).c_str());
    ImGui::PopTextWrapPos();

    if (!p.found) {
        ImGui::TextColored(kRed, "Não encontrado: %s", p.error.c_str());
    } else if (p.engineVersion.empty()) {
        ImGui::TextColored(kOrange, "Motor: versão desconhecida");
    } else if (FindVersion(m_versions, p.engineVersion)) {
        ImGui::TextColored(kGreen, "Motor %s", p.engineVersion.c_str());
    } else {
        ImGui::TextColored(kOrange, "Motor %s (não instalada)", p.engineVersion.c_str());
    }
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::TextColored(kDim, "Aberto: %s", FormatLastOpened(lastOpened, NowSeconds()).c_str());
    ImGui::EndGroup();

    // Duplo clique no cartão também abre.
    if (p.found && ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        RequestOpen(index, false);
    }

    ImGui::SameLine(startX + innerWidth - buttonsWidth + 16.0f);
    ImGui::BeginGroup();
    ImGui::BeginDisabled(!p.found || m_versions.empty());
    if (PrimaryButton("Abrir", ImVec2(110, 0))) RequestOpen(index, false);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("...", ImVec2(44, 0))) ImGui::OpenPopup("MenuProjeto");
    if (ImGui::BeginPopup("MenuProjeto")) {
        if (ImGui::MenuItem("Abrir com outra versão...", nullptr, false, p.found && !m_versions.empty())) {
            RequestOpen(index, true);
        }
        if (ImGui::MenuItem("Mostrar no Explorer")) platform::RevealInExplorer(p.found ? p.file : p.root);
        ImGui::Separator();
        if (ImGui::MenuItem("Remover da lista")) {
            std::string message;
            const bool ok = RemoveProject(p.root, message);
            SetStatus(message, !ok);
        }
        ImGui::EndPopup();
    }
    ImGui::EndGroup();

    ImGui::EndChild();
    ImGui::PopID();
}

void HubApp::RequestOpen(size_t index, bool chooseVersion) {
    if (index >= m_projects.size()) return;
    const ProjectInfo p = m_projects[index];  // cópia: abrir relê a lista
    const VersionChoice choice = ChooseVersion(p.engineVersion, m_versions, m_config.defaultVersion);
    if (choice.match == VersionMatch::NoVersions) {
        SetStatus("Nenhuma versão do Motor instalada: adicione uma na aba Versões.", true);
        return;
    }
    if (choice.match == VersionMatch::Exact && !chooseVersion) {
        std::string message;
        const bool ok = OpenProject(p.root, *choice.version, message);
        SetStatus(message, !ok);
        return;
    }
    // Versão ausente, desconhecida ou escolha pedida: pergunta (com o aviso de cópia de segurança).
    m_openWithRoot = p.root;
    m_openWithVersion = choice.version ? choice.version->version : std::string();
    m_openOpenWith = true;
}

void HubApp::DrawOpenWithPopup() {
    constexpr const char* kTitle = "Abrir com outra versão";
    if (m_openOpenWith) {
        ImGui::OpenPopup(kTitle);
        m_openOpenWith = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_NoSavedSettings)) return;

    const ProjectInfo info = ReadProject(m_openWithRoot);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::Text("Projeto: %s", info.name.c_str());
    if (info.engineVersion.empty()) {
        ImGui::TextColored(kOrange, "Este projeto não diz em qual versão do Motor foi feito (projeto antigo).");
    } else if (!FindVersion(m_versions, info.engineVersion)) {
        ImGui::TextColored(kOrange, "Este projeto é do Motor %s, que não está instalado.", info.engineVersion.c_str());
    } else {
        ImGui::Text("Versão do projeto: Motor %s", info.engineVersion.c_str());
    }
    ImGui::Spacing();
    ImGui::SetNextItemWidth(220.0f);
    VersionCombo("Abrir com", m_versions, m_openWithVersion, m_config.defaultVersion);

    const bool changes = !m_openWithVersion.empty() && m_openWithVersion != info.engineVersion;
    if (changes) {
        ImGui::Spacing();
        ImGui::TextColored(kOrange,
                           "O editor vai passar o projeto para o Motor %s (campo engineVersion do .mproject). "
                           "Depois disso ele pode não abrir mais na versão anterior: faça uma cópia de "
                           "segurança antes (copie a pasta do projeto ou faça um commit).",
                           m_openWithVersion.c_str());
        if (!info.engineVersion.empty() && CompareVersions(m_openWithVersion, info.engineVersion) < 0) {
            ImGui::TextColored(kRed, "Atenção: o Motor %s é mais antigo que o do projeto; cenas e scripts podem "
                                     "não abrir.", m_openWithVersion.c_str());
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::Separator();

    const InstalledVersion* chosen = FindVersion(m_versions, m_openWithVersion);
    ImGui::BeginDisabled(!chosen);
    const std::string label = chosen ? std::format("Abrir no Motor {}", chosen->version) : std::string("Abrir");
    if (PrimaryButton(label.c_str())) {
        std::string message;
        const bool ok = OpenProject(m_openWithRoot, *chosen, message);
        SetStatus(message, !ok);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancelar") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void HubApp::DrawNewProjectPopup() {
    constexpr const char* kTitle = "Novo projeto";
    if (m_openNewProject) {
        ImGui::OpenPopup(kTitle);
        m_openNewProject = false;
    }
    ImGui::SetNextWindowSize(ImVec2(600, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_NoSavedSettings)) return;

    const float labelWidth = 60.0f;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Nome");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputText("##nome", &m_newName);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Pasta");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-100);
    ImGui::InputText("##pasta", &m_newParent);
    ImGui::SameLine();
    if (ImGui::Button("Escolher...", ImVec2(-1, 0))) {
        if (auto folder = platform::PickFolder(PathFromUtf8(m_newParent), L"Pasta onde o projeto será criado")) {
            m_newParent = ToUtf8(*folder);
        }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Versão");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(220.0f);
    VersionCombo("##versao", m_versions, m_newVersion, m_config.defaultVersion);

    // A pasta padrão (Documentos\Motor) pode ainda não existir: é criada ao confirmar.
    const fs::path parent = PathFromUtf8(m_newParent);
    std::error_code ec;
    const bool parentIsDefault = SamePath(parent, m_paths.defaultProjectsDir) && !fs::exists(parent, ec);
    std::string problem = parentIsDefault ? ValidateProjectName(m_newName) : CheckNewProject(parent, m_newName);
    const InstalledVersion* version = FindVersion(m_versions, m_newVersion);
    if (problem.empty() && !version) problem = "Escolha uma versão do Motor (instale uma na aba Versões).";

    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    if (problem.empty()) {
        ImGui::TextColored(kDim, "Será criado em: %s", ToUtf8(parent / PathFromUtf8(m_newName)).c_str());
    } else {
        ImGui::TextColored(kRed, "%s", problem.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::Separator();

    ImGui::BeginDisabled(!problem.empty());
    if (PrimaryButton("Criar e abrir")) {
        if (parentIsDefault) fs::create_directories(parent, ec);
        std::string message;
        const bool ok = CreateProject(parent, m_newName, *version, message);
        SetStatus(message, !ok);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancelar") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void HubApp::DrawVersionsTab() {
    ImGui::BeginDisabled(m_adding.valid());
    if (PrimaryButton("Adicionar de uma pasta...")) {
        if (auto folder = platform::PickFolder({}, L"Pasta montada de uma versão do Motor (com motor-version.json)")) {
            StartAddVersion(*folder);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Abrir a pasta das versões")) {
        std::error_code ec;
        fs::create_directories(m_paths.versionsDir, ec);
        platform::RevealInExplorer(m_paths.versionsDir);
    }
    ImGui::TextColored(kDim, "Instaladas em %s", ToUtf8(m_paths.versionsDir).c_str());
    ImGui::Spacing();

    const float statusHeight = m_status.empty() ? 0.0f : ImGui::GetFrameHeightWithSpacing() * 1.6f;
    ImGui::BeginChild("ListaVersoes", ImVec2(0, -statusHeight));
    const InstalledVersion* def = DefaultVersion(m_versions, m_config.defaultVersion);
    if (m_versions.empty()) {
        ImGui::TextColored(kOrange, "Nenhuma versão instalada: baixe uma abaixo ou adicione de uma pasta.");
    } else if (ImGui::BeginTable("Versoes", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                   ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("Versão", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("API de scripts", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Compilação", ImGuiTableColumnFlags_WidthFixed, 95.0f);
        ImGui::TableSetupColumn("Pasta", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 250.0f);
        ImGui::TableHeadersRow();
        for (const InstalledVersion& v : m_versions) {
            ImGui::PushID(v.version.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Motor %s", v.version.c_str());
            if (&v == def) {
                ImGui::SameLine();
                ImGui::TextColored(kGreen, "padrão");
            }
            ImGui::TableNextColumn();
            ImGui::Text("%d", v.scriptApiVersion);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(v.buildType.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(kDim, "%s", ToUtf8(v.dir).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ToUtf8(v.dir).c_str());
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(&v == def);
            if (ImGui::Button("Tornar padrão")) {
                std::string message;
                const bool ok = SetDefaultVersion(v.version, message);
                SetStatus(message, !ok);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Mostrar")) platform::RevealInExplorer(v.dir);
            ImGui::SameLine();
            if (ImGui::Button("Remover")) {
                m_removeVersion = v.version;
                m_openRemoveVersion = true;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    DrawRemoteSection();
    ImGui::Spacing();
    DrawServersSection();

    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kDim, "Versões vêm de um servidor de versões (MotorServidor, na rede local) ou de uma pasta "
                             "montada no código do Motor (cmake --build build --target motor_package). O editor "
                             "instalado precisa do MSYS2 (UCRT64) e do CMake para compilar os scripts dos projetos.");
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    DrawStatusLine(m_status, m_statusError);
}

void HubApp::DrawRemoteSection() {
    ImGui::SeparatorText("Disponíveis para baixar");
    ImGui::BeginDisabled(m_fetching.valid());
    if (ImGui::Button(m_fetching.valid() ? "Procurando..." : "Atualizar")) StartFetch();
    ImGui::EndDisabled();

    // Versões de todos os servidores, sem repetir (vale o primeiro servidor da lista que tiver a versão).
    struct Row {
        const distrib::Release* release;
        std::string server;
    };
    std::vector<Row> rows;
    for (const RemoteServer& s : m_remote) {
        if (!s.manifest) continue;
        for (const distrib::Release& r : s.manifest->releases) {
            const bool seen = std::any_of(rows.begin(), rows.end(), [&](const Row& row) {
                return row.release->version == r.version;
            });
            if (!seen) rows.push_back({&r, s.url});
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return CompareVersions(a.release->version, b.release->version) > 0;
    });

    if (!m_fetchedOnce) {
        ImGui::SameLine();
        ImGui::TextColored(kDim, "Procurando versões nos servidores...");
        return;
    }
    if (rows.empty()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kDim, m_config.servers.empty() ? "Nenhum servidor na lista (adicione um abaixo)."
                                                          : "Nenhuma versão encontrada nos servidores.");
        return;
    }
    if (!ImGui::BeginTable("Disponiveis", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                 ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
        return;
    }
    ImGui::TableSetupColumn("Versão", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Canal", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("API", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableSetupColumn("Compilação", ImGuiTableColumnFlags_WidthFixed, 85.0f);
    ImGui::TableSetupColumn("Data e tamanho", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableSetupColumn("Servidor", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 250.0f);
    ImGui::TableHeadersRow();
    for (const Row& row : rows) {
        const distrib::Release& r = *row.release;
        ImGui::PushID(r.version.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Motor %s", r.version.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.channel.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%d", r.scriptApiVersion);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.buildType.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%s  %s", distrib::FormatDate(r.date).c_str(), distrib::FormatSize(r.size).c_str());
        ImGui::TableNextColumn();
        ImGui::TextColored(kDim, "%s", row.server.c_str());
        ImGui::TableNextColumn();
        if (FindVersion(m_versions, r.version)) {
            ImGui::TextColored(kGreen, "Instalada");
        } else if (m_download && m_download->release.version == r.version) {
            distrib::ReleaseProgress p;
            {
                std::lock_guard lock(m_download->mutex);
                p = m_download->progress;
            }
            const float fraction = p.total ? static_cast<float>(static_cast<double>(p.done) / p.total) : 0.0f;
            const std::string overlay = std::format("{} de {}", distrib::FormatSize(p.done), distrib::FormatSize(p.total));
            ImGui::ProgressBar(fraction, ImVec2(150.0f, 0.0f), overlay.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Arquivo %zu de %zu: %s", p.filesDone + 1, p.filesTotal, p.currentFile.c_str());
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(m_download->cancel.load());
            if (ImGui::Button("Cancelar")) m_download->cancel = true;
            ImGui::EndDisabled();
        } else {
            const uint64_t staged = distrib::StagedBytes(m_paths.versionsDir, r);
            const std::string label = staged > 0 ? std::format("Retomar ({}%)", staged * 100 / std::max<uint64_t>(r.size, 1))
                                                 : std::string("Baixar");
            ImGui::BeginDisabled(m_download != nullptr);
            if (PrimaryButton(label.c_str())) StartDownload(row.server, r);
            ImGui::EndDisabled();
            if (staged > 0 && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s já baixados; o resto continua de onde parou.", distrib::FormatSize(staged).c_str());
            }
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void HubApp::DrawServersSection() {
    if (!ImGui::CollapsingHeader("Servidores", m_config.servers.empty() ? ImGuiTreeNodeFlags_DefaultOpen : 0)) return;
    std::string toRemove;
    for (const std::string& url : m_config.servers) {
        ImGui::PushID(url.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(url.c_str());
        ImGui::SameLine(320.0f);
        const auto it = std::find_if(m_remote.begin(), m_remote.end(), [&](const RemoteServer& s) { return s.url == url; });
        if (m_fetching.valid() || it == m_remote.end()) {
            ImGui::TextColored(kDim, "...");
        } else if (it->manifest) {
            ImGui::TextColored(kGreen, "no ar, %zu versão(ões)", it->manifest->releases.size());
        } else {
            ImGui::TextColored(kRed, "%s", it->error.c_str());
        }
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 90.0f);
        if (ImGui::Button("Remover", ImVec2(90.0f, 0.0f))) toRemove = url;
        ImGui::PopID();
    }
    if (!toRemove.empty()) {
        std::string message;
        const bool ok = RemoveServer(toRemove, message);
        SetStatus(message, !ok);
        StartFetch();
    }
    ImGui::SetNextItemWidth(320.0f);
    const bool enter = ImGui::InputTextWithHint("##servidor", "http://servidor:47900", &m_newServer,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::BeginDisabled(m_newServer.empty());
    if (ImGui::Button("Adicionar servidor") || (enter && !m_newServer.empty())) {
        std::string message;
        const bool ok = AddServer(m_newServer, message);
        SetStatus(message, !ok);
        if (ok) {
            m_newServer.clear();
            StartFetch();
        }
    }
    ImGui::EndDisabled();
}

void HubApp::DrawHubUpdateNotice() {
    // Hub mais novo publicado em algum servidor: aviso com o link do instalador.
    const RemoteServer* newest = nullptr;
    for (const RemoteServer& s : m_remote) {
        if (!s.manifest || !s.manifest->hub) continue;
        if (CompareVersions(s.manifest->hub->version, MOTOR_HUB_VERSION) <= 0) continue;
        if (!newest || CompareVersions(s.manifest->hub->version, newest->manifest->hub->version) > 0) newest = &s;
    }
    if (!newest) return;
    const distrib::HubRelease& h = *newest->manifest->hub;
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kOrange, "Hub %s disponível em %s.", h.version.c_str(), newest->url.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Baixar o instalador")) {
        const char* file = h.Find(distrib::kSetupExe) ? distrib::kSetupExe : distrib::kHubExe;
        distrib::platform::OpenUrl(newest->url + distrib::HubFileUrl(file));
    }
}

void HubApp::DrawRemoveVersionPopup() {
    constexpr const char* kTitle = "Remover versão";
    if (m_openRemoveVersion) {
        ImGui::OpenPopup(kTitle);
        m_openRemoveVersion = false;
    }
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("Enviar o Motor %s para a Lixeira?", m_removeVersion.c_str());
    ImGui::TextColored(kDim, "Projetos desta versão vão pedir outra versão para abrir.");
    ImGui::Separator();
    if (PrimaryButton("Enviar para a Lixeira")) {
        std::string message;
        const bool ok = RemoveVersion(m_removeVersion, message);
        SetStatus(message, !ok);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void HubApp::StartAddVersion(const fs::path& source) {
    m_addingSource = source;
    const fs::path versionsDir = m_paths.versionsDir;
    m_adding = std::async(std::launch::async, [source, versionsDir] {
        AddResult result;
        result.version = InstallVersion(source, versionsDir, &result.error);
        return result;
    });
}

void HubApp::DrawAddingVersionPopup() {
    constexpr const char* kTitle = "Adicionando versão";
    if (!m_adding.valid() && !ImGui::IsPopupOpen(kTitle)) return;

    bool done = !m_adding.valid();
    if (!done && m_adding.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const AddResult result = m_adding.get();
        RefreshProjects();
        if (result.version) {
            SetStatus(std::format("Motor {} instalado em '{}'.", result.version->version, ToUtf8(result.version->dir)),
                      false);
        } else {
            SetStatus(result.error, true);
        }
        done = true;
    }
    if (!done && !ImGui::IsPopupOpen(kTitle)) ImGui::OpenPopup(kTitle);
    if (ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (done) ImGui::CloseCurrentPopup();
        ImGui::Text("Copiando %s ...", ToUtf8(m_addingSource).c_str());
        ImGui::TextColored(kDim, "Pode levar alguns segundos (a pasta tem centenas de MB).");
        ImGui::EndPopup();
    }
}

} // namespace hub
