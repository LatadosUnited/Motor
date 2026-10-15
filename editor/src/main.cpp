#include "EditorApp.h"

#include <exception>
#include <filesystem>

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
// Uso: MotorEditor [pasta-do-projeto | arquivo.mproject]
std::filesystem::path ProjectFromCommandLine([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
#ifdef _WIN32
    // argv no Windows vem na página de código ANSI; pegamos a versão wide para não perder acentos.
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    std::filesystem::path result;
    if (args && count > 1) result = args[1];
    LocalFree(args);
    return result;
#else
    return argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path{};
#endif
}
} // namespace

int main(int argc, char** argv) {
    try {
        editor::EditorApp app(ProjectFromCommandLine(argc, argv));
        app.Run();
    } catch (const std::exception& e) {
        motor::log::Error("Erro fatal: {}", e.what());
        return 1;
    }
    return 0;
}
