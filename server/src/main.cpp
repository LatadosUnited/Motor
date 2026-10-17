// MotorServidor.exe: servidor de versões do Motor (Fase 15). Publica versões montadas pelo alvo motor_package
// numa pasta e serve essa pasta por HTTP para o Hub e o instalador baixarem. Programa de console.
#include "ServerCore.h"

#include <Platform.h>

#include <HubCore.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <exception>
#include <string>
#include <thread>
#include <vector>

#ifndef MOTOR_SERVER_VERSION
#define MOTOR_SERVER_VERSION "0.0.0"
#endif

namespace {

namespace fs = std::filesystem;

constexpr const char* kUsage =
    "Uso: MotorServidor [--pasta <dir>] [opções] [ação]\n"
    "Sem ação, serve a pasta de publicações por HTTP até ser fechado (Ctrl+C).\n"
    "  --pasta <dir>                 pasta de publicações (padrão: a pasta Publicacoes ao lado do .exe, se\n"
    "                                existir; senão %LOCALAPPDATA%\\Motor\\Servidor)\n"
    "  --porta <n>                   porta (padrão 47900)\n"
    "  --local                       só aceita conexões desta máquina (127.0.0.1); sem isso, a rede local\n"
    "  --version                     mostra a versão do servidor e sai\n"
    "Ações:\n"
    "  --publicar <pasta montada> [--canal <nome>]   copia uma versão (alvo motor_package) e refaz o manifesto\n"
    "  --publicar-hub <MotorHub.exe> [--setup <MotorSetup.exe>]\n"
    "                                publica o Hub e o instalador (padrão: o MotorSetup.exe ao lado do Hub)\n"
    "  --remover <versão>            tira a versão do manifesto e manda a pasta para a Lixeira\n"
    "  --regenerar                   refaz o manifesto a partir das pastas\n"
    "  --listar                      lista o que está publicado\n";

void Print(const std::string& text) {
    std::fputs((text + "\n").c_str(), stdout);
    std::fflush(stdout);
}

int Fail(const std::string& message) {
    std::fputs((message + "\n").c_str(), stderr);
    std::fflush(stderr);
    return 1;
}

std::atomic<bool> g_stop{false};
void OnSignal(int) { g_stop = true; }

int List(const fs::path& root) {
    std::string error;
    const auto manifest = distrib::LoadManifest(root, &error);
    if (!manifest) return Fail(error);
    Print("Pasta de publicações: " + hub::ToUtf8(root));
    if (manifest->hub) {
        std::string files;
        for (const auto& f : manifest->hub->files) files += " " + f.path;
        Print("Hub " + manifest->hub->version + ":" + files);
    } else {
        Print("Hub: não publicado");
    }
    Print("Versões:");
    if (manifest->releases.empty()) Print("  (nenhuma)");
    for (const distrib::Release& r : manifest->releases) {
        Print("  Motor " + r.version + "  canal " + r.channel + "  API " + std::to_string(r.scriptApiVersion) + "  " +
              r.buildType + "  " + distrib::FormatDate(r.date) + "  " + distrib::FormatSize(r.size) + "  " +
              std::to_string(r.files.size()) + " arquivos");
    }
    return 0;
}

int Serve(const fs::path& root, const std::string& host, int port) {
    std::error_code ec;
    fs::create_directories(root, ec);
    distrib::DistribServer server(root);
    server.log = [](const std::string& line) { Print(line); };
    std::string error;
    if (server.Start(host, port, &error) < 0) return Fail(error);
    Print("Servidor de versões do Motor " + std::string(MOTOR_SERVER_VERSION) + " em http://" + host + ":" +
          std::to_string(server.Port()) + "/");
    Print("Pasta de publicações: " + hub::ToUtf8(root));
    if (host == "0.0.0.0") {
        Print("Aberto para a rede local: nos outros computadores use http://<IP desta máquina>:" +
              std::to_string(server.Port()) + " (o Firewall do Windows pode pedir permissão).");
    }
    Print("Ctrl+C para fechar.");
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
    while (!g_stop) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    server.Stop();
    Print("Servidor fechado.");
    return 0;
}

int ServerMain() {
    const std::vector<std::string> args = distrib::platform::CommandLineArgs();
    fs::path root;
    std::string host = "0.0.0.0";
    int port = distrib::kDefaultPort;
    std::string action;
    std::string actionArg;
    std::string channel;
    fs::path setup;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const auto next = [&](std::string& out) {
            if (i + 1 >= args.size()) return false;
            out = args[++i];
            return true;
        };
        std::string value;
        if (arg == "--help" || arg == "-h") {
            Print(kUsage);
            return 0;
        } else if (arg == "--version") {
            Print(std::string("Servidor de versões do Motor ") + MOTOR_SERVER_VERSION);
            return 0;
        } else if (arg == "--local") {
            host = "127.0.0.1";
        } else if (arg == "--pasta" && next(value)) {
            root = hub::PathFromUtf8(value);
        } else if (arg == "--porta" && next(value)) {
            try {
                port = std::stoi(value);
            } catch (const std::exception&) {
                port = -1;
            }
            if (port <= 0 || port > 65535) return Fail("Porta inválida: " + value);
        } else if (arg == "--canal" && next(value)) {
            channel = value;
        } else if (arg == "--setup" && next(value)) {
            setup = hub::PathFromUtf8(value);
        } else if ((arg == "--publicar" || arg == "--publicar-hub" || arg == "--remover") && next(value)) {
            action = arg;
            actionArg = value;
        } else if (arg == "--listar" || arg == "--regenerar") {
            action = arg;
        } else {
            return Fail("Opção desconhecida ou incompleta: " + arg + "\n" + kUsage);
        }
    }
    if (root.empty()) {
        root = distrib::DefaultPublishRoot(distrib::platform::ExecutablePath().parent_path(),
                                           distrib::platform::KnownFolder(distrib::platform::Folder::LocalAppData) /
                                               "Motor" / "Servidor");
    }
    std::error_code ec;
    root = fs::absolute(root, ec);

    std::string error;
    if (action.empty()) return Serve(root, host, port);
    if (action == "--listar") return List(root);
    if (action == "--regenerar") {
        if (!distrib::RegenerateManifest(root, &error)) return Fail(error);
        return List(root);
    }
    if (action == "--publicar") {
        Print("Calculando o SHA-256 dos arquivos e copiando...");
        const auto release = distrib::PublishRelease(hub::PathFromUtf8(actionArg), root, channel, &error);
        if (!release) return Fail(error);
        Print("Motor " + release->version + " publicado (" + std::to_string(release->files.size()) + " arquivos, " +
              distrib::FormatSize(release->size) + ") em " + hub::ToUtf8(distrib::ReleaseDir(root, release->version)));
        return 0;
    }
    if (action == "--publicar-hub") {
        const fs::path hubExe = hub::PathFromUtf8(actionArg);
        const std::string version = distrib::platform::FileVersion(hubExe);
        if (version.empty()) return Fail("'" + actionArg + "' não tem a versão do Hub (recurso VERSIONINFO).");
        if (setup.empty()) {
            setup = hubExe.parent_path() / distrib::kSetupExe;
            if (!fs::is_regular_file(setup, ec)) {
                Print("Aviso: " + hub::ToUtf8(setup) + " não encontrado; publicando só o Hub.");
                setup.clear();
            }
        }
        const auto published = distrib::PublishHub(hubExe, setup, version, root, &error);
        if (!published) return Fail(error);
        std::string files;
        for (const auto& f : published->files) files += " " + f.path;
        Print("Hub " + version + " publicado:" + files);
        return 0;
    }
    if (action == "--remover") {
        if (!distrib::UnpublishRelease(root, actionArg, [](const fs::path& p, std::string* e) {
                return distrib::platform::MoveToTrash(p, e);
            }, &error)) {
            return Fail(error);
        }
        Print("Motor " + actionArg + " removido (pasta na Lixeira).");
        return 0;
    }
    return Fail(kUsage);
}

} // namespace

int main() {
#ifdef _WIN32
    distrib::platform::AttachParentConsole();  // só põe o console em UTF-8 (já é um programa de console)
#endif
    try {
        return ServerMain();
    } catch (const std::exception& e) {
        return Fail(std::string("Erro: ") + e.what());
    }
}
