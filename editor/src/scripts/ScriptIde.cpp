#include "scripts/ScriptIde.h"

#include <motor/core/Log.h>
#include <motor/core/Paths.h>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace editor::ScriptIde {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;
using motor::ToGenericUtf8;
using motor::ToUtf8;

namespace {
std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// JSON com um comentário no topo (o VS Code lê esses arquivos como JSON com comentários).
std::string Jsonc(const std::string& comment, const json& value) {
    return std::format("{}. {}\n{}\n", kGeneratedMarker, comment, value.dump(2));
}

fs::path EnvPath(const wchar_t* name) {
#ifdef _WIN32
    const wchar_t* value = _wgetenv(name);
    return value && *value ? fs::path(value) : fs::path();
#else
    (void)name;
    return {};
#endif
}
} // namespace

std::vector<GeneratedFile> VsCodeFiles(const ScriptProject::Paths& paths, const ScriptProject::Toolchain& toolchain,
                                       const fs::path& editorExe) {
    const fs::path root = paths.scriptsDir.parent_path();
    const std::string build = "${workspaceFolder}/" + ToGenericUtf8(paths.binaryDir.lexically_relative(root));
    std::vector<GeneratedFile> files;

    // IntelliSense: as mesmas opções da compilação (headers do Motor, C++20, cabeçalho pré-compilado).
    files.push_back({"c_cpp_properties.json",
                     Jsonc("IntelliSense dos scripts com as opções da compilação do editor",
                           json{{"configurations",
                                 json::array({json{{"name", "Motor"},
                                                   {"compileCommands", build + "/compile_commands.json"},
                                                   {"compilerPath", ToGenericUtf8(toolchain.cxxCompiler)},
                                                   {"cppStandard", "c++20"},
                                                   {"intelliSenseMode", "windows-gcc-x64"},
                                                   {"includePath", json::array({"${workspaceFolder}/Scripts"})}}})},
                                {"version", 4}})});

    // Compilar pelo VS Code (Ctrl+Shift+B): o mesmo comando do editor, com os erros no painel Problemas.
    std::string pathEnv;
    for (const fs::path& dir : ScriptProject::ToolchainPathDirs(toolchain)) pathEnv += ToUtf8(dir) + ";";
    pathEnv += "${env:PATH}";
    std::vector<std::string> command = ScriptProject::BuildCommand(paths, toolchain);
    json args = json::array();
    for (size_t i = 1; i < command.size(); ++i) args.push_back(command[i]);
    files.push_back({"tasks.json",
                     Jsonc("Compilar os scripts (o editor aberto já compila sozinho ao salvar)",
                           json{{"version", "2.0.0"},
                                {"tasks", json::array({json{
                                              {"label", "Motor: compilar scripts"},
                                              {"type", "process"},
                                              {"command", command.empty() ? std::string() : command[0]},
                                              {"args", args},
                                              {"options", json{{"env", json{{"PATH", pathEnv}}}}},
                                              {"problemMatcher", json{{"base", "$gcc"}, {"fileLocation", "absolute"}}},
                                              {"group", json{{"kind", "build"}, {"isDefault", true}}}}})}})});

    // Depurar: anexar o gdb ao editor aberto (os scripts rodam dentro dele, numa DLL).
    files.push_back({"launch.json",
                     Jsonc("Depurar os scripts anexando o gdb ao Motor Editor (pacote mingw-w64-ucrt-x86_64-gdb do MSYS2)",
                           json{{"version", "0.2.0"},
                                {"configurations",
                                 json::array({json{{"name", "Motor: anexar ao editor (gdb)"},
                                                   {"type", "cppdbg"},
                                                   {"request", "attach"},
                                                   {"program", ToGenericUtf8(editorExe)},
                                                   {"processId", "${command:pickProcess}"},
                                                   {"MIMode", "gdb"},
                                                   {"miDebuggerPath",
                                                    ToGenericUtf8(toolchain.cxxCompiler.parent_path() / "gdb.exe")},
                                                   {"setupCommands",
                                                    json::array({json{{"text", "-enable-pretty-printing"},
                                                                      {"ignoreFailures", true}}})}}})}})});

    files.push_back({"extensions.json",
                     Jsonc("Extensão de C++ do VS Code (IntelliSense e depurador)",
                           json{{"recommendations", json::array({"ms-vscode.cpptools"})}})});
    return files;
}

int WriteVsCodeFiles(const ScriptProject::Paths& paths, const ScriptProject::Toolchain& toolchain,
                     const fs::path& editorExe, std::string* error) {
    const fs::path dir = paths.scriptsDir.parent_path() / ".vscode";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        if (error) *error = std::format("Não foi possível criar '{}': {}", ToUtf8(dir), ec.message());
        return -1;
    }
    int written = 0;
    for (const GeneratedFile& file : VsCodeFiles(paths, toolchain, editorExe)) {
        const fs::path path = dir / file.name;
        if (fs::exists(path, ec)) {
            const std::string current = ReadFile(path);
            if (!current.starts_with(kGeneratedMarker) || current == file.text) continue;  // do usuário, ou igual
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out || !(out << file.text) || !out.flush()) {
            if (error) *error = std::format("Não foi possível escrever '{}'.", ToUtf8(path));
            return -1;
        }
        ++written;
    }
    return written;
}

fs::path FindVsCode() {
    std::error_code ec;
    std::vector<fs::path> candidates;
    if (const fs::path local = EnvPath(L"LOCALAPPDATA"); !local.empty()) {
        candidates.push_back(local / "Programs" / "Microsoft VS Code" / "Code.exe");
    }
    for (const wchar_t* var : {L"ProgramFiles", L"ProgramW6432"}) {
        if (const fs::path pf = EnvPath(var); !pf.empty()) candidates.push_back(pf / "Microsoft VS Code" / "Code.exe");
    }
    // `code` no PATH: ...\Microsoft VS Code\bin\code.cmd, com o Code.exe na pasta de cima.
    if (const fs::path pathVar = EnvPath(L"PATH"); !pathVar.empty()) {
        std::wstringstream dirs(pathVar.native());
        std::wstring dir;
        while (std::getline(dirs, dir, L';')) {
            if (dir.empty()) continue;
            if (fs::is_regular_file(fs::path(dir) / "code.cmd", ec)) candidates.push_back(fs::path(dir).parent_path() / "Code.exe");
        }
    }
    for (const fs::path& candidate : candidates) {
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }
    return {};
}

std::vector<std::wstring> VsCodeCommand(const fs::path& code, const fs::path& projectRoot, const fs::path& file,
                                        int line, int column) {
    std::vector<std::wstring> args = {code.native(), projectRoot.native()};
    if (!file.empty()) {
        std::wstring target = file.native();
        if (line > 0) target += L":" + std::to_wstring(line) + (column > 0 ? L":" + std::to_wstring(column) : L"");
        args.push_back(L"--goto");
        args.push_back(target);
    }
    return args;
}

namespace {
#ifdef _WIN32
bool Launch(const std::vector<std::wstring>& args) {
    std::wstring commandLine;
    for (const std::wstring& arg : args) {
        if (!commandLine.empty()) commandLine += L' ';
        commandLine += L'"' + arg + L'"';  // caminhos do Windows não têm aspas
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(args[0].c_str(), commandLine.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr,
                        nullptr, &si, &pi)) {
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
#else
bool Launch(const std::vector<std::wstring>&) { return false; }
#endif
} // namespace

void OpenFile(const fs::path& projectRoot, const fs::path& file, int line, int column) {
    if (const fs::path code = FindVsCode(); !code.empty() && Launch(VsCodeCommand(code, projectRoot, file, line, column))) {
        return;
    }
#ifdef _WIN32
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, file.parent_path().c_str(), SW_SHOWNORMAL));
    if (result <= 32) motor::log::Warn("Nenhum programa abre '{}'", ToUtf8(file.filename()));
#endif
}

bool OpenProjectFolder(const fs::path& projectRoot) {
    const fs::path code = FindVsCode();
    return !code.empty() && Launch(VsCodeCommand(code, projectRoot, {}, 0, 0));
}

} // namespace editor::ScriptIde
