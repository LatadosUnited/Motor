#include "EditorApp.h"

#include <motor/core/Paths.h>

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

// Uso: MotorEditor [pasta-do-projeto | arquivo.mproject] [--mcp[=porta]]
editor::EditorApp::Options ParseOptions(const std::vector<std::string>& args) {
    editor::EditorApp::Options options;
    for (const std::string& arg : args) {
        if (arg == "--mcp") {
            options.mcpPort = editor::mcp::Server::kDefaultPort;
        } else if (arg.starts_with("--mcp=")) {
            try {
                options.mcpPort = std::stoi(arg.substr(6));
            } catch (const std::exception&) {
                motor::log::Warn("Porta inválida em {}: usando {}", arg, editor::mcp::Server::kDefaultPort);
                options.mcpPort = editor::mcp::Server::kDefaultPort;
            }
        } else if (options.project.empty()) {
            options.project = motor::PathFromUtf8(arg);
        }
    }
    return options;
}
} // namespace

int main(int argc, char** argv) {
    try {
        editor::EditorApp app(ParseOptions(Arguments(argc, argv)));
        app.Run();
    } catch (const std::exception& e) {
        motor::log::Error("Erro fatal: {}", e.what());
        return 1;
    }
    return 0;
}
