// MotorHub.exe: Hub do Motor (Fase 14). Sem argumentos abre a janela; com uma ação (--create, --open...) faz o
// mesmo que o botão correspondente e sai, escrevendo o resultado no console (agentes e testes automáticos).
#include "HubApp.h"

#include <InstallPlan.h>
#include <Platform.h>

#include <charconv>
#include <cstdio>
#include <exception>
#include <functional>
#include <string>
#include <vector>

#ifndef MOTOR_HUB_VERSION
#define MOTOR_HUB_VERSION "0.0.0"
#endif

namespace {

constexpr const char* kUsage =
    "Uso: MotorHub [opções] [ações] [-- argumentos a mais para o editor]\n"
    "Sem ações, abre a janela do hub.\n"
    "Opções:\n"
    "  --data <pasta>             pasta de dados do hub (padrão %LOCALAPPDATA%\\Motor: hub.json e Versões\\;\n"
    "                             o hub instalado usa a do instalacao.json ao lado do MotorHub.exe)\n"
    "  --aba <projetos|versões|mcp>  abre a janela nesta aba\n"
    "  --bandeja                  abre escondido, só com o ícone na bandeja (é assim que inicia com o Windows)\n"
    "  --mcp-porta <porta>        porta do MCP do hub (padrão 47600; 0 desliga): testes ao lado do hub do usuário\n"
    "  --version                  mostra a versão do hub e sai\n"
    "Ações (feitas na ordem; para na primeira que falhar):\n"
    "  --list                     lista as versões instaladas e os projetos\n"
    "  --add-version <pasta>      instala uma versão montada (alvo motor_package) em Versões\\<versão>\n"
    "  --remove-version <versão>  envia a versão para a Lixeira\n"
    "  --set-default <versão>     versão padrão para projetos novos\n"
    "  --add-project <pasta>      põe um projeto existente na lista\n"
    "  --remove-project <pasta>   tira o projeto da lista (a pasta fica)\n"
    "  --create <pasta-pai> <nome> [--engine <versão>]  cria o projeto com o editor daquela versão (padrão: a padrão)\n"
    "  --open <pasta> [--engine <versão>]               abre com a versão do projeto (ou a indicada; o editor\n"
    "                             atualiza o engineVersion do projeto: faça uma cópia de segurança antes)\n"
    "  --disponiveis [--servidor <URL>]                 lista as versões publicadas nos servidores\n"
    "  --baixar <versão|mais-recente> [--servidor <URL>]\n"
    "                             baixa a versão de um servidor (padrão: os do hub.json), confere o SHA-256 de\n"
    "                             cada arquivo e instala em Versões\\<versão>; retoma um download interrompido\n";

struct Action {
    std::string name;
    std::vector<std::string> args;
    std::string engine;  // --engine
    std::string server;  // --servidor
};

void Print(const std::string& text) {
    std::fputs(text.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

int Fail(const std::string& message) {
    std::fputs((message + "\n").c_str(), stderr);
    std::fflush(stderr);
    return 1;
}

int RunAction(hub::HubApp& app, const Action& a) {
    std::string message;
    bool ok = false;
    if (a.name == "--disponiveis") {
        const auto servers =
            hub::HubApp::FetchServers(a.server.empty() ? app.Config().servers : std::vector<std::string>{a.server});
        for (const auto& s : servers) {
            if (!s.manifest) {
                Print(s.url + ": " + s.error);
                continue;
            }
            Print(s.url + ":");
            if (s.manifest->releases.empty()) Print("  (nenhuma versão publicada)");
            for (const distrib::Release& r : s.manifest->releases) {
                const bool installed = hub::FindVersion(app.Versions(), r.version) != nullptr;
                Print("  Motor " + r.version + "  canal " + r.channel + "  API " + std::to_string(r.scriptApiVersion) +
                      "  " + r.buildType + "  " + distrib::FormatDate(r.date) + "  " + distrib::FormatSize(r.size) +
                      (installed ? "  (instalada)" : ""));
            }
            if (s.manifest->hub) {
                const bool newer = hub::CompareVersions(s.manifest->hub->version, MOTOR_HUB_VERSION) > 0;
                Print("  Hub " + s.manifest->hub->version + (newer ? " (mais novo que este)" : ""));
            }
        }
        return 0;
    }
    if (a.name == "--baixar") {
        int lastPercent = -1;
        ok = app.DownloadVersion(a.args[0], a.server, [&](const distrib::ReleaseProgress& p) {
            const int percent = p.total ? static_cast<int>(p.done * 100 / p.total) : 0;
            if (percent / 10 != lastPercent / 10 || (p.filesDone == p.filesTotal && percent != lastPercent)) {
                lastPercent = percent;
                Print("  " + std::to_string(percent) + "% (" + distrib::FormatSize(p.done) + " de " +
                      distrib::FormatSize(p.total) + ", " + std::to_string(p.filesDone) + "/" +
                      std::to_string(p.filesTotal) + " arquivos)");
            }
        }, message);
        if (!ok) return Fail(message);
        Print(message);
        return 0;
    }
    if (a.name == "--list") {
        Print("Versões instaladas (" + hub::ToUtf8(app.GetPaths().versionsDir) + "):");
        const hub::InstalledVersion* def = hub::DefaultVersion(app.Versions(), app.Config().defaultVersion);
        for (const auto& v : app.Versions()) {
            Print("  " + v.version + (&v == def ? " (padrão)" : "") + "  API de scripts " +
                  std::to_string(v.scriptApiVersion) + "  " + hub::ToUtf8(v.dir));
        }
        Print("Projetos:");
        for (size_t i = 0; i < app.Projects().size(); ++i) {
            const hub::ProjectInfo& p = app.Projects()[i];
            const std::string version = !p.found ? "não encontrado: " + p.error
                                        : p.engineVersion.empty() ? "versão desconhecida"
                                                                  : "Motor " + p.engineVersion;
            Print("  " + p.name + "  [" + version + "]  " + hub::ToUtf8(p.root) + "  aberto: " +
                  hub::FormatLastOpened(app.Config().projects[i].lastOpened, hub::NowSeconds()));
        }
        return 0;
    }
    if (a.name == "--add-version") {
        ok = app.AddVersion(hub::PathFromUtf8(a.args[0]), message);
    } else if (a.name == "--remove-version") {
        ok = app.RemoveVersion(a.args[0], message);
    } else if (a.name == "--set-default") {
        ok = app.SetDefaultVersion(a.args[0], message);
    } else if (a.name == "--add-project") {
        ok = app.AddProjectFolder(hub::PathFromUtf8(a.args[0]), message);
    } else if (a.name == "--remove-project") {
        ok = app.RemoveProject(hub::PathFromUtf8(a.args[0]), message);
    } else if (a.name == "--create") {
        if (const hub::InstalledVersion* v = app.PickVersion(a.engine, message)) {
            ok = app.CreateProject(hub::PathFromUtf8(a.args[0]), a.args[1], *v, message);
        }
    } else if (a.name == "--open") {
        ok = app.OpenProjectAuto(hub::PathFromUtf8(a.args[0]), a.engine, message);
    }
    if (!ok) return Fail(message);
    Print(message);
    return 0;
}

int HubMain() {
    const std::vector<std::string> args = hub::platform::CommandLineArgs();

    std::filesystem::path dataDir;
    std::vector<Action> actions;
    std::vector<std::string> editorArgs;
    hub::HubApp::WindowOptions window;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const size_t remaining = args.size() - i - 1;
        const auto needs = [&](size_t count) {
            if (remaining < count) {
                hub::platform::AttachParentConsole();
                Fail(arg + " precisa de " + std::to_string(count) + " argumento(s).\n" + kUsage);
                return false;
            }
            return true;
        };
        if (arg == "--") {
            editorArgs.assign(args.begin() + static_cast<std::ptrdiff_t>(i) + 1, args.end());
            break;
        } else if (arg == "--version" || arg == "--help" || arg == "-h") {
            hub::platform::AttachParentConsole();
            Print(arg == "--version" ? std::string("Hub do Motor ") + MOTOR_HUB_VERSION : std::string(kUsage));
            return 0;
        } else if (arg == "--data") {
            if (!needs(1)) return 2;
            dataDir = hub::PathFromUtf8(args[++i]);
        } else if (arg == "--aba") {
            if (!needs(1)) return 2;
            const std::string tab = args[++i];
            window.initialTab = tab == "versões" || tab == "versoes" ? 1 : tab == "mcp" ? 2 : 0;
        } else if (arg == distrib::kTrayArgument) {
            window.tray = true;
        } else if (arg == "--mcp-porta") {
            if (!needs(1)) return 2;
            const std::string& value = args[++i];
            int port = -1;
            std::from_chars(value.data(), value.data() + value.size(), port);
            if (port < 0 || port > 65535) {
                hub::platform::AttachParentConsole();
                return Fail("Porta inválida em --mcp-porta: " + value);
            }
            window.mcpPort = port;
        } else if (arg == "--servidor") {
            if (!needs(1)) return 2;
            if (actions.empty() || (actions.back().name != "--baixar" && actions.back().name != "--disponiveis")) {
                hub::platform::AttachParentConsole();
                return Fail("--servidor vem depois de --baixar ou --disponiveis.");
            }
            actions.back().server = args[++i];
        } else if (arg == "--engine") {
            if (!needs(1)) return 2;
            if (actions.empty() || (actions.back().name != "--create" && actions.back().name != "--open")) {
                hub::platform::AttachParentConsole();
                return Fail("--engine vem depois de --create ou --open.");
            }
            actions.back().engine = args[++i];
        } else if (arg == "--list" || arg == "--disponiveis") {
            actions.push_back({arg, {}, {}, {}});
        } else if (arg == "--add-version" || arg == "--remove-version" || arg == "--set-default" ||
                   arg == "--add-project" || arg == "--remove-project" || arg == "--open" || arg == "--baixar") {
            if (!needs(1)) return 2;
            actions.push_back({arg, {args[i + 1]}, {}, {}});
            ++i;
        } else if (arg == "--create") {
            if (!needs(2)) return 2;
            actions.push_back({arg, {args[i + 1], args[i + 2]}, {}, {}});
            i += 2;
        } else {
            hub::platform::AttachParentConsole();
            Fail("Opção desconhecida: " + arg + "\n" + kUsage);
            return 2;
        }
    }
    const bool console = !actions.empty();
    window.relaunchArgs = args;  // depois de se atualizar, o hub novo abre com a mesma linha

    // Hub instalado pelo MotorSetup com outra pasta de dados: ela fica no instalacao.json ao lado do .exe.
    if (dataDir.empty()) {
        const auto record = distrib::InstallRecord::Load(distrib::platform::ExecutablePath().parent_path() /
                                                         distrib::kInstallRecordFile);
        if (record && !record->dataDir.empty()) dataDir = record->dataDir;
    }

    try {
        hub::HubApp app(hub::platform::DefaultPaths(dataDir), std::move(editorArgs), dataDir);
        if (!console) return app.RunWindow(window);

        hub::platform::AttachParentConsole();
        for (const Action& action : actions) {
            if (const int code = RunAction(app, action); code != 0) return code;
        }
        return 0;
    } catch (const std::exception& e) {
        hub::platform::AttachParentConsole();
        return Fail(std::string("Erro: ") + e.what());
    }
}

} // namespace

#ifdef _WIN32
// Programa de janela (sem console): o Windows chama WinMain.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return HubMain();
}
#else
int main() {
    return HubMain();
}
#endif
