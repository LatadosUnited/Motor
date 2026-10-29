// Autoatualização do Hub (Hub residente, Tarefa 9): procura um Hub mais novo nos servidores, baixa o instalador
// (SHA-256 do manifesto conferido), abre-o em silêncio e sai. O MotorSetup --atualizar espera este hub fechar, troca o
// MotorHub.exe com as mesmas escolhas da instalação e abre o hub novo na bandeja; os editores abertos continuam e o
// hub novo volta a acompanhá-los pelo editores.json.

#include "HubApp.h"

#include <GLFW/glfw3.h>

#include <Platform.h>

#include <format>
#include <fstream>

#ifndef MOTOR_HUB_VERSION
#define MOTOR_HUB_VERSION "0.0.0"
#endif

namespace hub {

namespace {
// Tempo entre abrir o instalador e sair: a resposta do MCP (hub_update) sai antes de o servidor desligar.
constexpr double kQuitDelaySeconds = 0.5;

fs::path UpdateDir(const platform::Paths& paths) {
    return paths.dataDir / kUpdateDir;
}

fs::path SetupFile(const platform::Paths& paths, const std::string& version) {
    return UpdateDir(paths) / PathFromUtf8(std::format("MotorSetup-{}.exe", version));
}
} // namespace

std::optional<HubApp::HubUpdate> HubApp::NewestHub(const std::vector<RemoteServer>& servers) {
    std::optional<HubUpdate> best;
    for (const RemoteServer& s : servers) {
        if (!s.manifest || !s.manifest->hub) continue;
        const distrib::HubRelease& h = *s.manifest->hub;
        const distrib::ReleaseFile* setup = h.Find(distrib::kSetupExe);
        if (!setup || CompareVersions(h.version, MOTOR_HUB_VERSION) <= 0) continue;
        if (!best || CompareVersions(h.version, best->version) > 0) best = HubUpdate{h.version, s.url, *setup};
    }
    return best;
}

bool HubApp::CanSelfUpdate(std::string* why) const {
    const auto refuse = [&](std::string text) {
        if (why) *why = std::move(text);
        return false;
    };
    const fs::path exeDir = distrib::platform::ExecutablePath().parent_path();
    std::error_code ec;
    if (!fs::exists(exeDir / distrib::kInstallRecordFile, ec)) {
        return refuse(std::format("este Hub não foi instalado pelo MotorSetup (falta {} em '{}')",
                                  distrib::kInstallRecordFile, ToUtf8(exeDir)));
    }
    if (m_hubUpdate || m_quitForUpdate) return refuse("a atualização do Hub já está em andamento");
    if (m_download) return refuse(std::format("espere o download do Motor {} terminar", m_download->release.version));
    if (m_adding.valid()) return refuse("espere a versão que está sendo adicionada terminar de copiar");
    return true;
}

bool HubApp::StartHubUpdate(const HubUpdate& update, std::string& message) {
    if (!CanSelfUpdate(&message)) return false;
    auto job = std::make_unique<HubUpdateJob>();
    job->update = update;
    job->dest = SetupFile(m_paths, update.version);
    HubUpdateJob* raw = job.get();
    raw->result = std::async(std::launch::async, [raw] {
        distrib::Client client(raw->update.server);
        auto result = client.DownloadFile(distrib::HubFileUrl(distrib::kSetupExe), raw->dest, raw->update.setup,
                                          &raw->cancel, [raw](uint64_t bytes) { raw->bytes = bytes; });
        glfwPostEmptyEvent();  // acorda o loop da janela
        return result;
    });
    m_hubUpdate = std::move(job);
    m_hubUpdateMessage.clear();
    message = std::format("Baixando o Hub {} de {} ({})...", update.version, update.server,
                          distrib::FormatSize(update.setup.size));
    SetStatus(message, false);
    return true;
}

void HubApp::PollHubUpdate() {
    if (!m_hubUpdate || m_hubUpdate->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const distrib::Client::FileResult result = m_hubUpdate->result.get();
    const HubUpdate update = m_hubUpdate->update;
    const fs::path setup = m_hubUpdate->dest;
    m_hubUpdate.reset();
    const auto fail = [&](std::string text) {
        m_hubUpdateOk = false;
        m_hubUpdateMessage = std::move(text);
        SetStatus(m_hubUpdateMessage, true);
    };
    if (result.status != distrib::Status::Ok) {
        fail(result.status == distrib::Status::Cancelled
                 ? std::string("Atualização do Hub cancelada (o download continua depois de onde parou).")
                 : std::format("Atualização do Hub {}: {}", update.version, result.error));
        return;
    }
    // Conferido pelo SHA-256: abre o instalador, que espera este processo fechar.
    const fs::path installDir = distrib::platform::ExecutablePath().parent_path();
    const std::vector<std::string> args = UpdateInstallerArgs(installDir, platform::CurrentProcessId(), m_relaunchArgs,
                                                              UpdateDir(m_paths) / kUpdateLogFile);
    std::string error;
    if (!platform::LaunchProcess(setup, args, &error)) {
        fail(std::format("Atualização do Hub {}: {}", update.version, error));
        return;
    }
    m_hubUpdateOk = true;
    m_hubUpdateMessage = std::format("Instalando o Hub {}: o Hub fecha e volta na bandeja em alguns segundos (os "
                                     "editores abertos continuam).",
                                     update.version);
    SetStatus(m_hubUpdateMessage, false);
    if (m_tray) m_tray->Notify(std::format("Atualizando o Hub para {}", update.version), "Ele volta na bandeja sozinho.");
    m_quitForUpdate = true;
    m_quitAt = glfwGetTime() + kQuitDelaySeconds;
}

void HubApp::SaveOpenEditors() {
    const fs::path file = m_paths.dataDir / kEditorsFile;
    SavedEditors saved;
    for (const OpenEditor& e : Editors()) {
        const InstalledVersion* v = FindVersion(m_versions, e.version);
        if (!v) continue;
        saved.editors.push_back({e.process.Pid(), e.root, e.version, v->editor, e.mcpPort, e.token, e.ready});
    }
    std::error_code ec;
    if (saved.editors.empty()) {
        fs::remove(file, ec);  // estado nosso, refeito a cada saída
        return;
    }
    std::string error;
    if (!saved.Save(file, &error)) std::fprintf(stderr, "%s\n", error.c_str());
}

void HubApp::AdoptSavedEditors() {
    const fs::path file = m_paths.dataDir / kEditorsFile;
    std::error_code ec;
    if (!fs::exists(file, ec)) return;
    const auto saved = SavedEditors::Load(file);
    fs::remove(file, ec);  // vale uma vez: o próximo hub recebe a lista de quem sair agora
    if (!saved) return;
    for (const SavedEditor& s : saved->editors) {
        if (FindEditor(s.root)) continue;
        auto process = platform::ChildProcess::Adopt(s.pid, s.editor);
        if (!process) continue;  // fechou enquanto o hub estava fora
        m_editors.push_back({s.root, s.version, std::move(*process), NowSeconds(), s.mcpPort, s.token, s.ready});
    }
}

void HubApp::ReadUpdateLog() {
    const fs::path dir = UpdateDir(m_paths);
    const fs::path log = dir / kUpdateLogFile;
    std::error_code ec;
    if (fs::exists(log, ec)) {
        std::ifstream in(log, std::ios::binary);
        std::string line, last, error;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            last = line;
            if (line.rfind("Erro: ", 0) == 0) error = line.substr(6);
        }
        in.close();
        if (!error.empty()) {
            m_hubUpdateOk = false;
            m_hubUpdateMessage = "A atualização do Hub falhou: " + error;
        } else if (!last.empty()) {
            m_hubUpdateOk = true;
            m_hubUpdateMessage = last;  // "Hub atualizado para 0.4.0."
        }
        if (!m_hubUpdateMessage.empty()) SetStatus(m_hubUpdateMessage, !m_hubUpdateOk);
        fs::remove(log, ec);  // log nosso: já mostrado
    }
    // Instaladores já usados (desta versão ou mais velhos) saem; o de uma versão nova pela metade fica para retomar.
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const std::string name = ToUtf8(entry.path().filename());
        constexpr std::string_view prefix = "MotorSetup-";
        if (name.rfind(prefix, 0) != 0) continue;
        std::string version = name.substr(prefix.size());
        if (version.ends_with(".exe")) version.resize(version.size() - 4);
        if (CompareVersions(version, MOTOR_HUB_VERSION) <= 0) fs::remove(entry.path(), ec);  // download nosso
    }
}

} // namespace hub
