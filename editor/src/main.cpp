#include "EditorApp.h"
#include "mcp/McpToolList.h"

#include <motor/core/Paths.h>
#include <motor/core/Version.h>
#include <motor/scene/ScriptModule.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
std::vector<std::string> Arguments([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    std::vector<std::string> args;
#ifdef _WIN32
    // argv no Windows vem na página de código ANSI; pegamos a versão wide para não perder acentos.
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; wide && i < count; ++i) args.push_back(motor::ToUtf8(std::filesystem::path(wide[i])));
    LocalFree(wide);
#else
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
#endif
    return args;
}

constexpr const char* kUsage =
    "Uso: MotorEditor [pasta-do-projeto | arquivo.mproject] [opções]\n"
    "  --project <pasta>          abre o projeto (o mesmo que passar a pasta sozinha)\n"
    "  --create <pasta-pai> <nome> cria o projeto <pasta-pai>/<nome> (como Arquivo > Novo projeto) e abre\n"
    "  --settings <arquivo>       usa outro arquivo de configuração do editor\n"
    "  --no-settings              não lê nem grava a configuração do editor (%APPDATA%\\Motor\\editor.json)\n"
    "  --mcp[=porta]              liga o servidor MCP (porta padrão 47800)\n"
    "  --mcp-ferramentas <arquivo> grava a lista de ferramentas do MCP (JSON) sem abrir a janela e sai\n"
    "  --version                  mostra a versão e sai\n";

struct ParsedOptions {
    editor::EditorApp::Options app;
    bool exit = false;  // --version/--help: já respondeu, não abre o editor
    int exitCode = 0;
    std::filesystem::path toolList;  // --mcp-ferramentas
};

// Uso: ver kUsage. Os caminhos chegam em UTF-8 (Arguments).
ParsedOptions ParseOptions(const std::vector<std::string>& args) {
    ParsedOptions parsed;
    editor::EditorApp::Options& options = parsed.app;
    const auto fail = [&](const std::string& message) {
        std::fprintf(stderr, "%s\n%s", message.c_str(), kUsage);
        parsed.exit = true;
        parsed.exitCode = 2;
        return parsed;
    };
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const size_t remaining = args.size() - i - 1;
        if (arg == "--version") {
            std::printf("Motor Editor %s (API de scripts %d)\n", motor::kVersion, motor::kScriptApiVersion);
            parsed.exit = true;
            return parsed;
        } else if (arg == "--help" || arg == "-h") {
            std::printf("%s", kUsage);
            parsed.exit = true;
            return parsed;
        } else if (arg == "--project") {
            if (remaining < 1) return fail("--project precisa da pasta do projeto.");
            options.project = motor::PathFromUtf8(args[++i]);
        } else if (arg == "--create") {
            if (remaining < 2) return fail("--create precisa da pasta-pai e do nome do projeto.");
            options.createParent = motor::PathFromUtf8(args[++i]);
            options.createName = args[++i];
        } else if (arg == "--settings") {
            if (remaining < 1) return fail("--settings precisa do arquivo.");
            editor::EditorSettings::UseFile(motor::PathFromUtf8(args[++i]));
        } else if (arg == "--no-settings") {
            editor::EditorSettings::Disable();
        } else if (arg == "--mcp") {
            options.mcpPort = editor::mcp::Server::kDefaultPort;
        } else if (arg.starts_with("--mcp=")) {
            try {
                options.mcpPort = std::stoi(arg.substr(6));
            } catch (const std::exception&) {
                motor::log::Warn("Porta inválida em {}: usando {}", arg, editor::mcp::Server::kDefaultPort);
                options.mcpPort = editor::mcp::Server::kDefaultPort;
            }
        } else if (arg == "--mcp-ferramentas") {
            if (remaining < 1) return fail("--mcp-ferramentas precisa do arquivo.");
            parsed.toolList = motor::PathFromUtf8(args[++i]);
        } else if (arg == "--perf") {
            if (remaining < 1) return fail("--perf precisa do arquivo.");
            options.perfFile = motor::PathFromUtf8(args[++i]);
        } else if (arg == "--segundos") {
            if (remaining < 1) return fail("--segundos precisa do tempo.");
            try {
                options.seconds = std::stod(args[++i]);
            } catch (const std::exception&) {
                return fail("Tempo inválido em --segundos: " + args[i]);
            }
        } else if (arg.starts_with("--")) {
            return fail("Opção desconhecida: " + arg);
        } else if (options.project.empty()) {
            options.project = motor::PathFromUtf8(arg);
        }
    }
    return parsed;
}
} // namespace

int main(int argc, char** argv) {
    const ParsedOptions parsed = ParseOptions(Arguments(argc, argv));
    if (parsed.exit) return parsed.exitCode;
    if (!parsed.toolList.empty()) {
        // Hub residente: o motor_package grava a lista no pacote da versão; nada de janela nem configuração.
        std::string error;
        try {
            if (editor::mcp::WriteEditorToolList(parsed.toolList, error)) return 0;
        } catch (const std::exception& e) {
            error = e.what();
        }
        std::fprintf(stderr, "Não foi possível gravar a lista de ferramentas do MCP: %s\n", error.c_str());
        return 1;
    }
    try {
        editor::EditorApp app(parsed.app);
        app.Run();
    } catch (const std::exception& e) {
        motor::log::Error("Erro fatal: {}", e.what());
        return 1;
    }
    return 0;
}
