#include "SetupApp.h"

#include <HubCore.h>
#include <HubPlatform.h>
#include <Platform.h>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_stdlib.h>

#include <format>
#include <stdexcept>

#ifndef MOTOR_HUB_VERSION
#define MOTOR_HUB_VERSION "0.0.0"
#endif

namespace setup {

using hub::PathFromUtf8;
using hub::ToUtf8;

namespace {
const ImVec4 kGreen(0.45f, 0.85f, 0.55f, 1.0f);
const ImVec4 kOrange(0.95f, 0.65f, 0.25f, 1.0f);
const ImVec4 kRed(0.95f, 0.40f, 0.35f, 1.0f);
const ImVec4 kDim(0.60f, 0.62f, 0.66f, 1.0f);

void ApplyStyle() {
    // As mesmas cores do hub e do editor.
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 4.0f;
    s.FrameRounding = 3.0f;
    s.GrabRounding = 3.0f;
    s.FramePadding = ImVec2(8.0f, 5.0f);
    s.ItemSpacing = ImVec2(8.0f, 7.0f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.12f, 0.13f, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.14f, 0.15f, 0.17f, 1.00f);
    c[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.18f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.24f, 0.27f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.29f, 0.33f, 1.00f);
    c[ImGuiCol_CheckMark] = ImVec4(0.45f, 0.65f, 0.95f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.25f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.40f, 0.62f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
    c[ImGuiCol_PlotHistogram] = ImVec4(0.26f, 0.50f, 0.80f, 1.00f);
}

void LoadFont() {
    ImGuiIO& io = ImGui::GetIO();
    for (const char* path : {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"}) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && io.Fonts->AddFontFromFileTTF(path, 17.0f)) return;
    }
    io.Fonts->AddFontDefault();
}

bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.42f, 0.70f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.50f, 0.80f, 1.0f));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(2);
    return pressed;
}

// Rótulo à esquerda e campo de pasta com "Escolher..." à direita.
void FolderField(const char* label, const char* id, std::string& value, const wchar_t* title) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(150.0f);
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(-110.0f);
    ImGui::InputText("##campo", &value);
    ImGui::SameLine();
    if (ImGui::Button("Escolher...", ImVec2(-1, 0))) {
        if (auto folder = hub::platform::PickFolder(PathFromUtf8(value), title)) value = ToUtf8(*folder);
    }
    ImGui::PopID();
}

void Link(const char* text, const std::string& url) {
    ImGui::TextColored(ImVec4(0.48f, 0.70f, 1.0f, 1.0f), "%s", text);
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s", url.c_str());
    }
    if (ImGui::IsItemClicked()) distrib::platform::OpenUrl(url);
}
} // namespace

SetupApp::SetupApp(Options defaults, bool uninstall) : m_uninstall(uninstall), m_defaults(std::move(defaults)) {
    m_installDir = ToUtf8(m_defaults.install.installDir);
    m_dataDir = ToUtf8(m_defaults.install.dataDir.empty() ? DefaultDataDir() : m_defaults.install.dataDir);
    m_startMenu = m_defaults.install.startMenuShortcut;
    m_desktop = m_defaults.install.desktopShortcut;
    m_startup = m_defaults.install.startWithWindows;
    m_server = m_defaults.server.empty() ? std::string(distrib::kDefaultServer) : m_defaults.server;
    m_localVersion = ToUtf8(m_defaults.localVersion);
    m_motor = m_defaults.motor == MotorSource::Server ? 1 : m_defaults.motor == MotorSource::Folder ? 2 : 0;
    if (const auto existing = ExistingInstall(m_defaults.install.installDir)) {
        m_existing = std::format("O Hub {} já está instalado nesta pasta: instalar de novo atualiza o Hub (projetos, "
                                 "versões e hub.json ficam).",
                                 existing->hubVersion);
        if (!existing->dataDir.empty()) m_dataDir = ToUtf8(existing->dataDir);
    }
    RefreshTools();
}

void SetupApp::RefreshTools() {
    fs::path sdk;
    for (const auto& v : hub::ScanVersions(VersionsDir(PathFromUtf8(m_dataDir)))) {
        sdk = v.dir / "sdk" / "MotorSDK.json";
        break;
    }
    m_tools = distrib::CheckBuildTools(sdk);
}

Options SetupApp::CurrentOptions() const {
    Options o = m_defaults;
    o.install.installDir = PathFromUtf8(m_installDir);
    // A pasta de dados padrão não vai no instalacao.json: o hub instalado continua achando a dele sozinho.
    const fs::path data = PathFromUtf8(m_dataDir);
    o.install.dataDir = hub::SamePath(data, DefaultDataDir()) ? fs::path() : data;
    o.install.startMenuShortcut = m_startMenu;
    o.install.desktopShortcut = m_desktop;
    o.install.startWithWindows = m_startup;
    o.motor = m_motor == 1 ? MotorSource::Server : m_motor == 2 ? MotorSource::Folder : MotorSource::None;
    o.server = m_motor == 1 ? m_server : std::string();
    o.localVersion = PathFromUtf8(m_localVersion);
    return o;
}

int SetupApp::Run() {
    if (!glfwInit()) throw std::runtime_error("Não foi possível iniciar o GLFW.");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    const std::string title = m_uninstall ? std::string("Desinstalar o Hub do Motor")
                                          : std::format("Instalar o Hub do Motor {}", MOTOR_HUB_VERSION);
    m_window = glfwCreateWindow(760, m_uninstall ? 380 : 640, title.c_str(), nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("Não foi possível criar a janela (OpenGL 3.3).");
    }
    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ApplyStyle();
    LoadFont();
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    if (!m_uninstall && m_motor == 1) StartCheckServer();
    while (!glfwWindowShouldClose(m_window)) {
        const bool busy = m_work.valid() || m_check.valid();
        glfwWaitEventsTimeout(busy ? 0.05 : 0.5);
        if (m_check.valid() && m_check.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            m_checkResult = m_check.get();
        }
        if (m_work.valid() && m_work.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            m_success = m_work.get();
            m_page = Page::Done;
        }
        if (glfwGetWindowAttrib(m_window, GLFW_ICONIFIED)) continue;

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
    if (m_work.valid()) {
        m_cancel = true;  // fechou no meio: o download parcial fica para retomar pelo hub
        m_work.wait();
    }
    if (m_check.valid()) m_check.wait();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(m_window);
    glfwTerminate();
    return m_success ? 0 : 1;
}

void SetupApp::DrawFrame() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Instalador", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushFont(nullptr, 24.0f);
    ImGui::TextUnformatted(m_uninstall ? "Desinstalar o Hub do Motor" : "Hub do Motor");
    ImGui::PopFont();
    if (!m_uninstall) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kDim, "%s", MOTOR_HUB_VERSION);
    }
    ImGui::Spacing();
    switch (m_page) {
    case Page::Options: m_uninstall ? DrawUninstallOptions() : DrawOptions(); break;
    case Page::Working: DrawWorking(); break;
    case Page::Done: DrawDone(); break;
    }
    ImGui::End();
}

void SetupApp::DrawOptions() {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kDim, "O Hub abre e cria os projetos e instala as versões do Motor. A instalação é só para "
                             "você (sem pedir administrador).");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    FolderField("Pasta do Hub", "pasta", m_installDir, L"Pasta onde o Hub será instalado");
    FolderField("Dados do Hub", "dados", m_dataDir, L"Pasta das versões do Motor e do hub.json");
    ImGui::SetCursorPosX(150.0f);
    ImGui::TextColored(kDim, "versões do Motor (Versões\\) e a configuração do Hub (hub.json)");
    if (!m_existing.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kOrange, "%s", m_existing.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Checkbox("Atalho no menu Iniciar", &m_startMenu);
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::Checkbox("Atalho na área de trabalho", &m_desktop);
    ImGui::Checkbox("Iniciar com o Windows (o Hub fica na bandeja)", &m_startup);

    ImGui::SeparatorText("Versão do Motor (opcional)");
    ImGui::RadioButton("Agora não (dá para baixar depois, na aba Versões do Hub)", &m_motor, 0);
    if (ImGui::RadioButton("Baixar a versão mais recente de um servidor", &m_motor, 1)) StartCheckServer();
    if (m_motor == 1) {
        ImGui::Indent(28.0f);
        ImGui::SetNextItemWidth(320.0f);
        if (ImGui::InputText("##servidor", &m_server, ImGuiInputTextFlags_EnterReturnsTrue)) StartCheckServer();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_check.valid());
        if (ImGui::Button(m_check.valid() ? "Verificando..." : "Verificar")) StartCheckServer();
        ImGui::EndDisabled();
        if (!m_check.valid() && m_checkedServer == m_server && !m_checkResult.text.empty()) {
            ImGui::TextColored(m_checkResult.ok ? kGreen : kRed, "%s", m_checkResult.text.c_str());
        }
        ImGui::Unindent(28.0f);
    }
    ImGui::RadioButton("Instalar de uma pasta montada (alvo motor_package)", &m_motor, 2);
    if (m_motor == 2) {
        ImGui::Indent(28.0f);
        ImGui::SetNextItemWidth(-110.0f);
        ImGui::InputText("##local", &m_localVersion);
        ImGui::SameLine();
        if (ImGui::Button("Escolher...", ImVec2(-1, 0))) {
            if (auto folder = hub::platform::PickFolder(PathFromUtf8(m_localVersion),
                                                        L"Pasta montada de uma versão do Motor (com motor-version.json)")) {
                m_localVersion = ToUtf8(*folder);
            }
        }
        std::string error;
        if (!m_localVersion.empty()) {
            if (const auto v = hub::ReadInstalledVersion(PathFromUtf8(m_localVersion), &error)) {
                ImGui::TextColored(kGreen, "Motor %s (%s)", v->version.c_str(), v->buildType.c_str());
            } else {
                ImGui::TextColored(kRed, "%s", error.c_str());
            }
        }
        ImGui::Unindent(28.0f);
    }

    ImGui::SeparatorText("Para compilar os scripts dos projetos");
    bool missing = false;
    for (const distrib::BuildTool& tool : m_tools) {
        ImGui::TextColored(tool.found ? kGreen : kOrange, "%s %s", tool.found ? "Encontrado:" : "Não encontrado:",
                           tool.name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(kDim, "%s", ToUtf8(tool.path).c_str());
        missing = missing || !tool.found;
    }
    if (missing) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kDim, "O Hub e o editor funcionam sem eles, mas os scripts só compilam com o MSYS2 (UCRT64) "
                                 "e o CMake. Instale o MSYS2 e, no terminal UCRT64, rode: pacman -S "
                                 "mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake make");
        ImGui::PopTextWrapPos();
        Link("Baixar o MSYS2 (msys2.org)", distrib::kMsys2Url);
        ImGui::SameLine(0.0f, 24.0f);
        if (ImGui::SmallButton("Verificar de novo")) RefreshTools();
    }

    // Botões no rodapé.
    const float buttonsY = ImGui::GetWindowHeight() - ImGui::GetFrameHeight() - ImGui::GetStyle().WindowPadding.y;
    if (ImGui::GetCursorPosY() < buttonsY) ImGui::SetCursorPosY(buttonsY);
    std::string problem;
    if (m_installDir.empty()) {
        problem = "Escolha a pasta do Hub.";
    } else if (m_motor == 1) {
        distrib::ParseServerUrl(m_server, &problem);  // continua vazio se o endereço for válido
    } else if (m_motor == 2 && !hub::ReadInstalledVersion(PathFromUtf8(m_localVersion))) {
        problem = "Escolha uma pasta montada válida.";
    }
    ImGui::BeginDisabled(!problem.empty());
    if (PrimaryButton("Instalar", ImVec2(130, 0))) StartWork();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancelar", ImVec2(110, 0))) glfwSetWindowShouldClose(m_window, GLFW_TRUE);
    if (!problem.empty()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kRed, "%s", problem.c_str());
    }
}

void SetupApp::DrawUninstallOptions() {
    const auto record = ExistingInstall(PathFromUtf8(m_installDir));
    ImGui::PushTextWrapPos(0.0f);
    if (!record) {
        ImGui::TextColored(kRed, "Não há uma instalação do Hub em %s.", m_installDir.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Fechar", ImVec2(110, 0))) glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        return;
    }
    const fs::path data = record->dataDir.empty() ? DefaultDataDir() : record->dataDir;
    ImGui::Text("Hub %s em %s", record->hubVersion.c_str(), ToUtf8(record->installDir).c_str());
    ImGui::TextColored(kDim, "A pasta do Hub e os atalhos vão para a Lixeira. Os projetos não são tocados.");
    ImGui::Spacing();
    ImGui::Checkbox("Também mandar para a Lixeira as versões do Motor instaladas e o hub.json", &m_removeData);
    ImGui::TextColored(kDim, "em %s (sem marcar, ficam para uma próxima instalação)", ToUtf8(data).c_str());
    ImGui::PopTextWrapPos();

    const float buttonsY = ImGui::GetWindowHeight() - ImGui::GetFrameHeight() - ImGui::GetStyle().WindowPadding.y;
    if (ImGui::GetCursorPosY() < buttonsY) ImGui::SetCursorPosY(buttonsY);
    if (PrimaryButton("Desinstalar", ImVec2(130, 0))) StartWork();
    ImGui::SameLine();
    if (ImGui::Button("Cancelar", ImVec2(110, 0))) glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void SetupApp::StartCheckServer() {
    if (m_check.valid()) return;
    m_checkedServer = m_server;
    m_check = std::async(std::launch::async, [url = m_server] {
        Check check;
        distrib::Client client(url);
        std::string error;
        const auto manifest = client.FetchManifest(&error);
        if (!manifest) {
            check.text = error;
        } else if (const distrib::Release* r = manifest->Latest()) {
            check.ok = true;
            check.text = std::format("Motor {} ({}, {}, {}) disponível.", r->version, r->buildType,
                                     distrib::FormatSize(r->size), distrib::FormatDate(r->date));
        } else {
            check.text = "O servidor não tem nenhuma versão publicada.";
        }
        glfwPostEmptyEvent();
        return check;
    });
}

void SetupApp::StartWork() {
    m_page = Page::Working;
    m_log.clear();
    m_error.clear();
    const Options options = CurrentOptions();
    const bool removeData = m_removeData;
    const fs::path installDir = PathFromUtf8(m_installDir);
    const auto log = [this](const std::string& line) {
        std::lock_guard lock(m_mutex);
        m_log.push_back(line);
        glfwPostEmptyEvent();
    };
    if (m_uninstall) {
        m_work = std::async(std::launch::async, [this, installDir, removeData, log] {
            std::string error;
            const bool ok = Uninstall(installDir, removeData, log, error);
            std::lock_guard lock(m_mutex);
            m_error = error;
            return ok;
        });
        return;
    }
    m_work = std::async(std::launch::async, [this, options, log] {
        std::string error;
        const bool ok = Install(options, log, [this](const distrib::ReleaseProgress& p) {
            std::lock_guard lock(m_mutex);
            m_progress = p;
            m_downloading = true;
        }, &m_cancel, error);
        std::lock_guard lock(m_mutex);
        m_error = error;
        m_downloading = false;
        return ok;
    });
}

void SetupApp::DrawWorking() {
    std::lock_guard lock(m_mutex);
    ImGui::TextUnformatted(m_uninstall ? "Desinstalando..." : "Instalando...");
    if (m_downloading) {
        const float fraction = m_progress.total ? static_cast<float>(static_cast<double>(m_progress.done) / m_progress.total)
                                                : 0.0f;
        const std::string overlay = std::format("{} de {}", distrib::FormatSize(m_progress.done),
                                                distrib::FormatSize(m_progress.total));
        ImGui::ProgressBar(fraction, ImVec2(-120.0f, 0.0f), overlay.c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(m_cancel.load());
        if (ImGui::Button("Cancelar", ImVec2(-1, 0))) m_cancel = true;
        ImGui::EndDisabled();
        ImGui::TextColored(kDim, "Arquivo %zu de %zu: %s", std::min(m_progress.filesDone + 1, m_progress.filesTotal),
                           m_progress.filesTotal, m_progress.currentFile.c_str());
    }
    ImGui::BeginChild("Log", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (const std::string& line : m_log) ImGui::TextWrapped("%s", line.c_str());
    ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

void SetupApp::DrawDone() {
    std::vector<std::string> log;
    std::string error;
    {
        std::lock_guard lock(m_mutex);
        log = m_log;
        error = m_error;
    }
    ImGui::PushTextWrapPos(0.0f);
    if (m_success) {
        ImGui::TextColored(kGreen, "%s", m_uninstall ? "Hub desinstalado." : "Hub instalado.");
    } else {
        ImGui::TextColored(kRed, "%s", error.c_str());
    }
    ImGui::PopTextWrapPos();
    const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f;
    ImGui::BeginChild("Log", ImVec2(0, -footer), ImGuiChildFlags_Borders);
    for (const std::string& line : log) {
        ImGui::TextColored(line.starts_with("Aviso") ? kOrange : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s",
                           line.c_str());
    }
    ImGui::EndChild();

    const bool hubInstalled = !m_uninstall && fs::exists(PathFromUtf8(m_installDir) / distrib::kHubExe);
    if (hubInstalled) ImGui::Checkbox("Abrir o Hub agora", &m_openHub);
    if (PrimaryButton("Concluir", ImVec2(130, 0))) {
        if (hubInstalled && m_openHub) {
            const Options o = CurrentOptions();
            std::vector<std::string> args;
            if (!o.install.dataDir.empty()) args = {"--data", ToUtf8(o.install.dataDir)};
            std::string ignored;
            hub::platform::LaunchProcess(PathFromUtf8(m_installDir) / distrib::kHubExe, args, &ignored);
        }
        glfwSetWindowShouldClose(m_window, GLFW_TRUE);
    }
}

} // namespace setup
