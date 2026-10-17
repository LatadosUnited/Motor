#include "Setup.h"

#include <HubCore.h>
#include <Platform.h>

#include <format>
#include <fstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifndef MOTOR_HUB_VERSION
#define MOTOR_HUB_VERSION "0.0.0"
#endif

namespace setup {

using distrib::platform::Folder;
using distrib::platform::KnownFolder;
using hub::PathFromUtf8;
using hub::ToUtf8;

namespace {
void Say(const LogFn& log, const std::string& line) {
    if (log) log(line);
}

uint64_t FileSize(const fs::path& p) {
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    return ec ? 0 : size;
}

std::string NowIso() {
    const std::time_t t = static_cast<std::time_t>(hub::NowSeconds());
    std::tm tm{};
    localtime_s(&tm, &t);
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                       tm.tm_hour, tm.tm_min, tm.tm_sec);
}

// Grava `bytes` em `dest` passando por .tmp (um hub aberto não fica pela metade).
bool WriteAtomic(const fs::path& dest, const std::string& bytes, std::string& error) {
    fs::path tmp = dest;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())) || !out.flush()) {
            error = std::format("Não foi possível escrever '{}'.", ToUtf8(dest));
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, dest, ec);
    if (ec) {
        fs::remove(tmp, ec);
        error = std::format("Não foi possível substituir '{}' (o Hub está aberto? Feche-o e tente de novo).",
                            ToUtf8(dest));
        return false;
    }
    return true;
}

bool InstallMotor(const Options& o, const fs::path& versionsDir, const LogFn& log, const ProgressFn& progress,
                  const std::atomic<bool>* cancel, std::string& error) {
    if (o.motor == MotorSource::Folder) {
        Say(log, std::format("Instalando o Motor da pasta '{}'...", ToUtf8(o.localVersion)));
        const auto installed = hub::InstallVersion(o.localVersion, versionsDir, &error);
        if (!installed) return false;
        Say(log, std::format("Motor {} instalado em '{}'.", installed->version, ToUtf8(installed->dir)));
        return true;
    }
    Say(log, std::format("Procurando a versão mais recente em {}...", o.server));
    distrib::Client client(o.server);
    const auto manifest = client.FetchManifest(&error);
    if (!manifest) return false;
    const distrib::Release* latest = manifest->Latest();
    if (!latest) {
        error = std::format("Nenhuma versão do Motor publicada em {}.", o.server);
        return false;
    }
    if (fs::exists(versionsDir / PathFromUtf8(latest->version))) {
        Say(log, std::format("Motor {} já está instalado.", latest->version));
        return true;
    }
    Say(log, std::format("Baixando o Motor {} ({}, {} arquivos)...", latest->version, distrib::FormatSize(latest->size),
                         latest->files.size()));
    const distrib::ReleaseOutcome outcome =
        distrib::DownloadRelease(o.server, *latest, versionsDir, cancel, progress);
    if (outcome.status != distrib::Status::Ok) {
        error = outcome.error;
        return false;
    }
    Say(log, std::format("Motor {} instalado em '{}' (SHA-256 de todos os arquivos conferido).", latest->version,
                         ToUtf8(outcome.dir)));
    return true;
}
} // namespace

fs::path DefaultInstallDir() { return KnownFolder(Folder::LocalAppData) / "Programs" / "Motor Hub"; }
fs::path DefaultDataDir() { return KnownFolder(Folder::LocalAppData) / "Motor"; }
fs::path VersionsDir(const fs::path& dataDir) {
    return (dataDir.empty() ? DefaultDataDir() : dataDir) / PathFromUtf8("Versões");
}

std::optional<std::string> EmbeddedHub(std::string* error) {
#ifdef _WIN32
    HRSRC res = FindResourceW(nullptr, L"MOTORHUB", MAKEINTRESOURCEW(10));  // RT_RCDATA
    HGLOBAL handle = res ? LoadResource(nullptr, res) : nullptr;
    const void* data = handle ? LockResource(handle) : nullptr;
    if (!data) {
        if (error) *error = "Este instalador não tem o Hub embutido (recurso MOTORHUB).";
        return std::nullopt;
    }
    return std::string(static_cast<const char*>(data), SizeofResource(nullptr, res));
#else
    if (error) *error = "Instalador só no Windows.";
    return std::nullopt;
#endif
}

std::optional<distrib::InstallRecord> ExistingInstall(const fs::path& installDir) {
    return distrib::InstallRecord::Load(installDir / distrib::kInstallRecordFile);
}

bool Install(const Options& options, const LogFn& log, const ProgressFn& progress, const std::atomic<bool>* cancel,
             std::string& error) {
    Options o = options;
    std::error_code ec;
    o.install.installDir = fs::absolute(o.install.installDir, ec).lexically_normal();
    if (!o.install.dataDir.empty()) o.install.dataDir = fs::absolute(o.install.dataDir, ec).lexically_normal();
    if (o.install.hubVersion.empty()) o.install.hubVersion = MOTOR_HUB_VERSION;
    if (o.motor == MotorSource::Server || !o.server.empty()) o.install.server = distrib::NormalizeServerUrl(o.server);
    if (o.motor == MotorSource::Server) {
        if (!distrib::ParseServerUrl(o.server, &error)) return false;
    }

    const auto hubBytes = EmbeddedHub(&error);
    if (!hubBytes) return false;
    const fs::path self = distrib::platform::ExecutablePath();
    o.install.installedBytes = hubBytes->size() + FileSize(self);

    const fs::path startMenu = o.shortcutsDir.empty() ? KnownFolder(Folder::StartMenuPrograms) : o.shortcutsDir;
    const fs::path desktop = o.shortcutsDir.empty() ? KnownFolder(Folder::Desktop) : o.shortcutsDir / "Desktop";
    distrib::InstallPlan plan = distrib::PlanInstall(o.install, startMenu, desktop);
    plan.record.installed = NowIso();

    // 1. Arquivos: o hub (do recurso) e uma cópia deste instalador, para desinstalar depois.
    Say(log, std::format("Instalando o Hub {} em '{}'...", o.install.hubVersion, ToUtf8(o.install.installDir)));
    fs::create_directories(o.install.installDir, ec);
    if (ec) {
        error = std::format("Não foi possível criar '{}': {}", ToUtf8(o.install.installDir), ec.message());
        return false;
    }
    if (!WriteAtomic(plan.hubExe, *hubBytes, error)) return false;
    if (!hub::SamePath(self, plan.setupExe)) {
        fs::path tmp = plan.setupExe;
        tmp += ".tmp";
        fs::copy_file(self, tmp, fs::copy_options::overwrite_existing, ec);
        if (!ec) fs::rename(tmp, plan.setupExe, ec);
        if (ec) {
            error = std::format("Não foi possível copiar o instalador para '{}': {}", ToUtf8(plan.setupExe), ec.message());
            return false;
        }
    }

    // 2. Atalhos e desinstalação no Painel de Controle (Aplicativos instalados).
    for (const distrib::Shortcut& s : plan.shortcuts) {
        if (!distrib::platform::CreateShortcut(s, &error)) return false;
        Say(log, "Atalho: " + ToUtf8(s.link));
    }
    if (!plan.registryKey.empty()) {
        if (!distrib::platform::WriteUninstallKey(plan.registryKey, plan.registryValues, &error)) return false;
        Say(log, "Desinstalação registrada em Aplicativos instalados (HKCU\\...\\Uninstall\\" + plan.registryKey + ").");
    }

    // 3. Registro da instalação: a desinstalação desfaz exatamente isso. Gravado antes do download: se ele falhar,
    // o hub já está instalado e desinstalável.
    if (!plan.record.Save(plan.recordFile, &error)) return false;

    // 4. hub.json: o servidor escolhido entra na lista do hub.
    const fs::path dataDir = o.install.dataDir.empty() ? DefaultDataDir() : o.install.dataDir;
    if (!o.install.server.empty()) {
        std::string warning;
        hub::HubConfig config = hub::HubConfig::Load(dataDir / "hub.json", &warning);
        if (config.AddServer(o.install.server)) {
            if (!config.Save(dataDir / "hub.json", &error)) return false;
            Say(log, "Servidor " + o.install.server + " adicionado ao hub.json.");
        }
    }

    // 5. A versão do Motor (opcional).
    if (o.motor != MotorSource::None && !InstallMotor(o, VersionsDir(o.install.dataDir), log, progress, cancel, error)) {
        error = "O Hub foi instalado, mas a versão do Motor não: " + error + " (dá para baixar depois pela aba Versões do Hub).";
        return false;
    }

    // Ferramentas para compilar os scripts: só avisa.
    fs::path sdk;
    for (const auto& v : hub::ScanVersions(VersionsDir(o.install.dataDir))) {
        sdk = v.dir / "sdk" / "MotorSDK.json";
        break;
    }
    for (const distrib::BuildTool& tool : distrib::CheckBuildTools(sdk)) {
        if (!tool.found) {
            Say(log, std::format("Aviso: {} não encontrado em '{}'. Os scripts dos projetos não vão compilar até instalar "
                                 "o MSYS2 UCRT64 com o CMake ({}).",
                                 tool.name, ToUtf8(tool.path), distrib::kMsys2Url));
        }
    }
    Say(log, "Pronto.");
    return true;
}

bool Uninstall(const fs::path& installDir, bool removeData, const LogFn& log, std::string& error) {
    const auto record = ExistingInstall(installDir);
    if (!record) {
        error = std::format("Não há uma instalação do Hub em '{}' (falta {}).", ToUtf8(installDir),
                            distrib::kInstallRecordFile);
        return false;
    }
    const distrib::UninstallPlan plan = distrib::PlanUninstall(*record, removeData, DefaultDataDir());

    // Atalhos: só os .lnk que o instalador criou.
    std::error_code ec;
    for (const fs::path& link : plan.shortcuts) {
        if (fs::remove(link, ec)) Say(log, "Atalho removido: " + ToUtf8(link));
    }
    if (!plan.registryKey.empty() && distrib::platform::UninstallKeyExists(plan.registryKey)) {
        if (distrib::platform::DeleteUninstallKey(plan.registryKey)) {
            Say(log, "Desinstalação tirada de Aplicativos instalados.");
        }
    }
    for (const fs::path& path : plan.toTrash) {
        if (!distrib::platform::MoveToTrash(path, &error)) {
            error += " Feche o Hub e o editor e tente de novo.";
            return false;
        }
        Say(log, "Para a Lixeira: " + ToUtf8(path));
    }
    if (!plan.emptyDirToRemove.empty() && fs::is_directory(plan.emptyDirToRemove, ec) &&
        fs::is_empty(plan.emptyDirToRemove, ec)) {
        fs::remove(plan.emptyDirToRemove, ec);  // vazia: nada a guardar
    }
    Say(log, "Hub desinstalado.");
    return true;
}

} // namespace setup
