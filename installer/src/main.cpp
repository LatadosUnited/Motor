// MotorSetup.exe: instalador do Hub do Motor (Fase 15). Sem argumentos abre a janela; --silencioso instala (ou,
// com --desinstalar, desinstala) sem janela, escrevendo no console de quem chamou (testes e agentes).
#include "Setup.h"
#include "SetupApp.h"

#include <HubCore.h>
#include <HubPlatform.h>
#include <Platform.h>

#include <cstdio>
#include <exception>
#include <format>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifndef MOTOR_HUB_VERSION
#define MOTOR_HUB_VERSION "0.0.0"
#endif

namespace {

namespace fs = std::filesystem;
using hub::PathFromUtf8;
using hub::ToUtf8;

constexpr const char* kUsage =
    "Uso: MotorSetup [opções]\n"
    "Sem --silencioso, abre a janela do instalador (as opções abaixo preenchem a tela).\n"
    "  --silencioso               instala sem janela\n"
    "  --pasta <dir>              pasta do Hub (padrão %LOCALAPPDATA%\\Programs\\Motor Hub)\n"
    "  --dados <dir>              pasta de dados do hub: Versões\\ e hub.json (padrão %LOCALAPPDATA%\\Motor)\n"
    "  --servidor <URL>           baixa a versão mais recente do Motor deste servidor (e põe no hub.json)\n"
    "  --versao-local <pasta>     instala o Motor de uma pasta montada (alvo motor_package)\n"
    "  --sem-atalhos              não cria atalhos\n"
    "  --area-de-trabalho         cria também o atalho na área de trabalho\n"
    "  --sem-registro             não registra a desinstalação em Aplicativos instalados\n"
    "  --abrir-hub                abre o Hub no fim\n"
    "  --desinstalar [--apagar-dados]\n"
    "                             desinstala (pasta do Hub, atalhos e registro para a Lixeira; com --apagar-dados,\n"
    "                             também as versões do Motor e o hub.json)\n"
    "Para testes: --atalhos-em <dir> (atalhos nesta pasta), --chave-registro <nome> (outra chave de desinstalação)\n"
    "  --version                  mostra a versão e sai\n";

void Print(const std::string& text) {
    std::fputs((text + "\n").c_str(), stdout);
    std::fflush(stdout);
}

int Fail(const std::string& message) {
    std::fputs((message + "\n").c_str(), stderr);
    std::fflush(stderr);
    return 1;
}

bool IsInside(const fs::path& file, const fs::path& dir) {
    std::error_code ec;
    const fs::path rel = fs::absolute(file, ec).lexically_normal().lexically_relative(fs::absolute(dir, ec).lexically_normal());
    return !rel.empty() && *rel.begin() != "..";
}

// A desinstalação manda a pasta do Hub para a Lixeira, e o Windows não move a pasta de um .exe aberto. Quando este
// instalador roda de dentro dela (Aplicativos instalados chama <pasta>\MotorSetup.exe), uma cópia na pasta
// temporária faz o trabalho e este processo fecha logo (esperar por ela manteria o .exe aberto). A saída do
// console vai para a cópia: quem lê por um pipe recebe tudo e só vê o fim quando ela termina. Devolve 0 se
// abriu a cópia, 1 se falhou, -1 se não precisou dela.
int RelaunchFromTemp(const fs::path& installDir, const std::vector<std::string>& args) {
    const fs::path self = distrib::platform::ExecutablePath();
    if (!IsInside(self, installDir)) return -1;
    const fs::path copy = distrib::platform::KnownFolder(distrib::platform::Folder::Temp) /
                          std::format("MotorSetup-desinstalar-{}.exe", GetCurrentProcessId());
    std::error_code ec;
    fs::copy_file(self, copy, fs::copy_options::overwrite_existing, ec);
    if (ec) return Fail("Não foi possível copiar o desinstalador para a pasta temporária: " + ec.message());

    std::vector<std::string> childArgs = args;
    childArgs.insert(childArgs.end(), {"--pasta", ToUtf8(installDir), "--esperar",
                                       std::to_string(GetCurrentProcessId()), "--copia-temporaria"});
    std::wstring line = hub::BuildCommandLine(copy, childArgs);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    // Saída padrão herdável (pipe ou arquivo de quem chamou), para a cópia escrever no mesmo lugar.
    const auto inheritable = [](DWORD which) -> HANDLE {
        const HANDLE h = GetStdHandle(which);
        HANDLE dup = nullptr;
        if (h && h != INVALID_HANDLE_VALUE &&
            DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
            return dup;
        }
        return nullptr;
    };
    HANDLE out = inheritable(STD_OUTPUT_HANDLE);
    HANDLE err = inheritable(STD_ERROR_HANDLE);
    if (out || err) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = out;
        si.hStdError = err;
    }
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(copy.c_str(), line.data(), nullptr, nullptr, TRUE, 0, nullptr,
                                   copy.parent_path().c_str(), &si, &pi);
    const DWORD lastError = GetLastError();
    if (out) CloseHandle(out);
    if (err) CloseHandle(err);
    if (!ok) return Fail(std::format("Não foi possível abrir o desinstalador (erro {}).", lastError));
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

// A cópia temporária se apaga depois de fechar (um cmd escondido espera alguns segundos e apaga só ela).
void DeleteSelfLater() {
    const fs::path self = distrib::platform::ExecutablePath();
    std::wstring line = L"cmd.exe /d /c ping -n 4 127.0.0.1 >nul & del /f /q \"" + self.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                       distrib::platform::KnownFolder(distrib::platform::Folder::Temp).c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

int SetupMain() {
    const std::vector<std::string> args = distrib::platform::CommandLineArgs();
    setup::Options o;
    o.install.installDir = setup::DefaultInstallDir();
    bool silent = false, uninstall = false, removeData = false, openHub = false, tempCopy = false;
    bool explicitDir = false;
    DWORD waitPid = 0;
    std::vector<std::string> passThrough;  // para a cópia temporária da desinstalação

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        std::string value;
        const auto next = [&] {
            if (i + 1 >= args.size()) return false;
            value = args[++i];
            return true;
        };
        if (arg == "--help" || arg == "-h") {
            distrib::platform::AttachParentConsole();
            Print(kUsage);
            return 0;
        } else if (arg == "--version") {
            distrib::platform::AttachParentConsole();
            Print(std::string("Instalador do Hub do Motor ") + MOTOR_HUB_VERSION);
            return 0;
        } else if (arg == "--silencioso") {
            silent = true;
        } else if (arg == "--desinstalar") {
            uninstall = true;
        } else if (arg == "--apagar-dados") {
            removeData = true;
        } else if (arg == "--sem-atalhos") {
            o.install.startMenuShortcut = o.install.desktopShortcut = false;
        } else if (arg == "--area-de-trabalho") {
            o.install.desktopShortcut = true;
        } else if (arg == "--sem-registro") {
            o.install.registerUninstall = false;
        } else if (arg == "--abrir-hub") {
            openHub = true;
        } else if (arg == "--copia-temporaria") {
            tempCopy = true;
            continue;
        } else if (arg == "--pasta" && next()) {
            o.install.installDir = PathFromUtf8(value);
            explicitDir = true;
            continue;  // a cópia temporária recebe a pasta resolvida
        } else if (arg == "--dados" && next()) {
            o.install.dataDir = PathFromUtf8(value);
        } else if (arg == "--servidor" && next()) {
            o.server = value;
            o.motor = setup::MotorSource::Server;
        } else if (arg == "--versao-local" && next()) {
            o.localVersion = PathFromUtf8(value);
            o.motor = setup::MotorSource::Folder;
        } else if (arg == "--atalhos-em" && next()) {
            o.shortcutsDir = PathFromUtf8(value);
        } else if (arg == "--chave-registro" && next()) {
            o.install.registryKey = value;
        } else if (arg == "--esperar" && next()) {
            waitPid = static_cast<DWORD>(std::stoul(value));
            continue;
        } else {
            distrib::platform::AttachParentConsole();
            return Fail("Opção desconhecida ou incompleta: " + arg + "\n" + kUsage);
        }
        passThrough.push_back(arg);
        if (!value.empty()) passThrough.push_back(value);
    }
    if (silent) distrib::platform::AttachParentConsole();
    std::error_code ec;
    o.install.installDir = fs::absolute(o.install.installDir, ec).lexically_normal();

    if (waitPid != 0) {
        // Cópia temporária: espera o desinstalador original fechar (ele está dentro da pasta a remover).
        if (HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, waitPid)) {
            WaitForSingleObject(parent, 30000);
            CloseHandle(parent);
        }
    }

    if (uninstall) {
        // Rodando de dentro de uma instalação sem --pasta: desinstala a própria.
        if (!explicitDir && !tempCopy) {
            const fs::path selfDir = distrib::platform::ExecutablePath().parent_path();
            if (fs::exists(selfDir / distrib::kInstallRecordFile, ec)) o.install.installDir = selfDir;
        }
        if (!tempCopy) {
            const int code = RelaunchFromTemp(o.install.installDir, passThrough);
            if (code >= 0) return code;
        }
        int result = 0;
        if (silent) {
            std::string error;
            const bool ok = setup::Uninstall(o.install.installDir, removeData, Print, error);
            result = ok ? 0 : Fail(error);
        } else {
            result = setup::SetupApp(o, true).Run();
        }
        if (tempCopy) DeleteSelfLater();
        return result;
    }

    if (!silent) return setup::SetupApp(o, false).Run();

    std::string error;
    const bool ok = setup::Install(o, Print, [last = -1](const distrib::ReleaseProgress& p) mutable {
        const int percent = p.total ? static_cast<int>(p.done * 100 / p.total) : 0;
        if (percent / 10 != last / 10) {
            last = percent;
            Print(std::format("  {}% ({} de {})", percent, distrib::FormatSize(p.done), distrib::FormatSize(p.total)));
        }
    }, nullptr, error);
    if (!ok) return Fail(error);
    if (openHub) {
        std::vector<std::string> hubArgs;
        if (!o.install.dataDir.empty()) hubArgs = {"--data", ToUtf8(o.install.dataDir)};
        hub::platform::LaunchProcess(o.install.installDir / distrib::kHubExe, hubArgs, &error);
    }
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        return SetupMain();
    } catch (const std::exception& e) {
        distrib::platform::AttachParentConsole();
        return Fail(std::string("Erro: ") + e.what());
    }
}
