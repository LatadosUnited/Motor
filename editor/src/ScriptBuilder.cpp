#include "ScriptBuilder.h"

#include <motor/core/Log.h>
#include <motor/core/Paths.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace editor {

namespace fs = std::filesystem;

#ifdef _WIN32
namespace {
std::wstring Widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string Narrow(const std::wstring& wide, UINT codePage = CP_UTF8) {
    if (wide.empty()) return {};
    const int n = WideCharToMultiByte(codePage, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(codePage, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    return out;
}

// A saída das ferramentas costuma ser UTF-8, mas algumas usam a página de código do sistema (acentos em
// caminhos). Se não for UTF-8 válido, converte da página ANSI.
std::string ToolOutputToUtf8(const std::string& text) {
    if (text.empty()) return text;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) > 0) {
        return text;
    }
    const int n = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), wide.data(), n);
    return Narrow(wide);
}

// Regras de aspas do CommandLineToArgvW / runtime do C.
std::wstring QuoteArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (size_t i = 0;; ++i) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++i;
            ++backslashes;
        }
        if (i == arg.size()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
        } else {
            out.append(backslashes, L'\\');
        }
        out += arg[i];
    }
    out += L'"';
    return out;
}

// Ambiente atual com `extraPath` na frente do PATH (bloco no formato do CreateProcessW).
std::wstring BuildEnvironment(const std::vector<fs::path>& extraPath) {
    std::wstring prefix;
    for (const fs::path& dir : extraPath) prefix += dir.wstring() + L";";

    std::wstring block;
    bool pathFound = false;
    if (wchar_t* env = GetEnvironmentStringsW()) {
        for (const wchar_t* var = env; *var; var += wcslen(var) + 1) {
            std::wstring entry = var;
            if (_wcsnicmp(entry.c_str(), L"PATH=", 5) == 0) {
                entry = L"PATH=" + prefix + entry.substr(5);
                pathFound = true;
            }
            block += entry;
            block.push_back(L'\0');
        }
        FreeEnvironmentStringsW(env);
    }
    if (!pathFound) {
        block += L"PATH=" + prefix;
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

std::string LastErrorMessage() {
    const DWORD code = GetLastError();
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::string message = buffer ? Narrow(buffer) : std::format("erro {}", code);
    if (buffer) LocalFree(buffer);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == '.')) message.pop_back();
    return message;
}
} // namespace

int RunProcess(const std::vector<std::string>& args, const std::vector<fs::path>& extraPath,
               const std::function<void(const std::string&)>& onOutput, const std::atomic<bool>& cancel,
               std::string* error) {
    std::wstring commandLine;
    for (const std::string& arg : args) {
        if (!commandLine.empty()) commandLine += L' ';
        commandLine += QuoteArg(Widen(arg));
    }

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        if (error) *error = "CreatePipe: " + LastErrorMessage();
        return -1;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    // Job: cancelar (ou fechar o editor) encerra também os filhos (make, compilador).
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job) SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    std::wstring env = BuildEnvironment(extraPath);
    const BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, env.data(),
                                        nullptr, &si, &pi);
    CloseHandle(writePipe);  // só o filho escreve: sem isso o ReadFile nunca veria o fim
    if (!created) {
        if (error) *error = std::format("Não foi possível executar '{}': {}", args.empty() ? "" : args[0], LastErrorMessage());
        CloseHandle(readPipe);
        if (job) CloseHandle(job);
        return -1;
    }
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    std::string pending;  // pedaço de linha ainda sem '\n' (não quebra caracteres UTF-8 ao meio)
    auto flushLines = [&](bool all) {
        const size_t end = all ? pending.size() : pending.rfind('\n');
        if (end == std::string::npos || end == 0) return;
        const size_t count = all ? end : end + 1;
        onOutput(ToolOutputToUtf8(pending.substr(0, count)));
        pending.erase(0, count);
    };

    char buffer[4096];
    bool cancelled = false;
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr)) break;  // todos fecharam
        if (available > 0) {
            DWORD read = 0;
            if (!ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
            pending.append(buffer, read);
            flushLines(false);
            continue;
        }
        if (cancel && !cancelled) {
            cancelled = true;
            if (job) TerminateJobObject(job, 1);
            TerminateProcess(pi.hProcess, 1);
        }
        if (WaitForSingleObject(pi.hProcess, 15) == WAIT_OBJECT_0) {
            // Terminou: lê o que sobrou no pipe e sai (mesmo que algum neto ainda segure o pipe aberto).
            if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) continue;
            break;
        }
    }
    flushLines(true);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(readPipe);
    if (job) CloseHandle(job);
    if (cancelled) {
        if (error) *error = "Cancelado.";
        return -1;
    }
    return static_cast<int>(exitCode);
}
#else
int RunProcess(const std::vector<std::string>&, const std::vector<fs::path>&, const std::function<void(const std::string&)>&,
               const std::atomic<bool>&, std::string* error) {
    if (error) *error = "Compilar scripts só é suportado no Windows por enquanto.";
    return -1;
}
#endif

namespace {
// Mapeia o objeto gerado pelo CMake (ex.: ".o/6445d702/6dbd6be7.obj") para o nome legível do fonte
// (ex.: "Girar.cpp" ou "MotorScriptEntry.cpp") a partir do compile_commands.json.
std::unordered_map<std::string, std::string> LoadObjectNames(const ScriptProject::Paths& paths) {
    std::unordered_map<std::string, std::string> names;
    std::error_code ec;
    const fs::path jsonPath = paths.binaryDir / "compile_commands.json";
    if (!fs::is_regular_file(jsonPath, ec)) return names;
    try {
        std::ifstream in(jsonPath, std::ios::binary);
        const auto j = nlohmann::json::parse(in, nullptr, false);
        if (!j.is_array()) return names;
        const fs::path scriptsNorm = paths.scriptsDir.lexically_normal();
        for (const auto& entry : j) {
            if (!entry.is_object()) continue;
            const std::string output = entry.value("output", std::string());
            const std::string file = entry.value("file", std::string());
            if (output.empty() || file.empty()) continue;
            const fs::path filePath = motor::PathFromUtf8(file).lexically_normal();
            const fs::path rel = filePath.lexically_relative(scriptsNorm);
            std::string display;
            if (!rel.empty() && rel.native()[0] != '.') {
                display = motor::ToGenericUtf8(rel);
            } else {
                display = motor::ToUtf8(filePath.filename());
            }
            names[output] = std::move(display);
        }
    } catch (...) {
    }
    return names;
}

std::string FallbackObjectName(const std::string& obj) {
    std::string name = motor::ToUtf8(motor::PathFromUtf8(obj).filename());
    if (name.size() > 4 && name.ends_with(".obj")) {
        const std::string stem = name.substr(0, name.size() - 4);
        if (stem.ends_with(".cpp") || stem.ends_with(".cc") || stem.ends_with(".cxx")) return stem;
    }
    return name;
}
} // namespace

// ---------------------------------------------------------------------------
// ScriptBuilder
// ---------------------------------------------------------------------------

ScriptBuilder::~ScriptBuilder() {
    Cancel();
}

void ScriptBuilder::Cancel() {
    if (!m_thread.joinable()) return;
    m_cancel = true;
    m_thread.join();
    m_cancel = false;
}

bool ScriptBuilder::Start(const ScriptProject::Paths& paths, const ScriptProject::Toolchain& toolchain) {
    if (IsRunning()) return false;
    if (m_thread.joinable()) m_thread.join();  // a anterior já terminou

    {
        std::lock_guard lock(m_mutex);
        m_log.clear();
        m_start = std::chrono::steady_clock::now();
        m_end = {};
        m_timings = {};
    }
    m_finished = false;
    m_cancel = false;
    m_state = State::Running;

    m_thread = std::thread([this, paths, toolchain] {
        using Clock = std::chrono::steady_clock;
        Timings timings;
        bool ok = true;
        std::string error;
        if (!ScriptProject::WriteBuildFiles(paths, toolchain, &error)) {
            Append(error + "\n");
            ok = false;
        }
        const auto pathDirs = ScriptProject::ToolchainPathDirs(toolchain);
        auto run = [&](const std::vector<std::string>& args,
                       const std::function<void(const std::string&)>& onChunk = {}) {
            std::string line;
            for (const std::string& a : args) line += (line.empty() ? "" : " ") + a;
            Append("> " + line + "\n");
            const int code = RunProcess(
                args, pathDirs,
                [this, &onChunk](const std::string& text) {
                    Append(text);
                    if (onChunk) onChunk(text);
                },
                m_cancel, &error);
            if (code == -1) Append(error + "\n");
            return code == 0;
        };
        if (ok && ScriptProject::NeedsConfigure(paths)) {
            ScriptProject::ResetConfigure(paths);
            const auto t0 = Clock::now();
            ok = run(ScriptProject::ConfigureCommand(paths, toolchain));
            timings.configure += std::chrono::duration<float>(Clock::now() - t0).count();
        }
        if (ok) {
            struct StartedFile {
                std::string obj;
                Clock::time_point startSteady{};
                fs::file_time_type startFile{};
            };
            std::vector<StartedFile> startedFiles;
            Clock::time_point reconfigStart{};
            Clock::time_point compileStart{};
            Clock::time_point linkStart{};

            auto parseLine = [&](std::string line) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const auto now = Clock::now();
                if (line.find("Re-running CMake...") != std::string::npos ||
                    (reconfigStart == Clock::time_point{} && compileStart == Clock::time_point{} &&
                     linkStart == Clock::time_point{} && line.rfind("-- ", 0) == 0)) {
                    if (reconfigStart == Clock::time_point{}) reconfigStart = now;
                }
                if (line.rfind("-- Build files have been written to:", 0) == 0 && reconfigStart != Clock::time_point{}) {
                    timings.configure += std::chrono::duration<float>(now - reconfigStart).count();
                    reconfigStart = {};
                }
                constexpr std::string_view kBuilding = "Building CXX object ";
                if (const size_t pos = line.find(kBuilding); pos != std::string::npos) {
                    std::string obj = line.substr(pos + kBuilding.size());
                    while (!obj.empty() && (obj.back() == ' ' || obj.back() == '\t')) obj.pop_back();
                    if (compileStart == Clock::time_point{}) compileStart = now;
                    startedFiles.push_back({std::move(obj), now, fs::file_time_type::clock::now()});
                }
                if (line.find("Linking CXX shared library ") != std::string::npos ||
                    line.find("Linking CXX shared module ") != std::string::npos) {
                    if (compileStart != Clock::time_point{} && timings.compile == 0.0f) {
                        timings.compile = std::chrono::duration<float>(now - compileStart).count();
                    }
                    if (linkStart == Clock::time_point{}) linkStart = now;
                }
                if (line.find("Built target ") != std::string::npos) {
                    if (linkStart != Clock::time_point{} && timings.link == 0.0f) {
                        timings.link = std::chrono::duration<float>(now - linkStart).count();
                    }
                }
            };

            ok = run(ScriptProject::BuildCommand(paths, toolchain), [&](const std::string& chunk) {
                std::istringstream in(chunk);
                std::string line;
                while (std::getline(in, line)) parseLine(std::move(line));
            });

            const auto buildEnd = Clock::now();
            if (reconfigStart != Clock::time_point{}) {
                timings.configure += std::chrono::duration<float>(buildEnd - reconfigStart).count();
            }
            if (compileStart != Clock::time_point{} && timings.compile == 0.0f) {
                const auto end = linkStart != Clock::time_point{} ? linkStart : buildEnd;
                timings.compile = std::chrono::duration<float>(end - compileStart).count();
            }
            if (linkStart != Clock::time_point{} && timings.link == 0.0f) {
                timings.link = std::chrono::duration<float>(buildEnd - linkStart).count();
            }

            const auto objNames = LoadObjectNames(paths);
            const auto compileEndSteady = linkStart != Clock::time_point{} ? linkStart : buildEnd;
            for (size_t i = 0; i < startedFiles.size(); ++i) {
                const StartedFile& sf = startedFiles[i];
                std::string name;
                if (auto it = objNames.find(sf.obj); it != objNames.end()) {
                    name = it->second;
                } else {
                    name = FallbackObjectName(sf.obj);
                }
                float sec = 0.0f;
                std::error_code ec;
                const fs::path objPath = paths.binaryDir / motor::PathFromUtf8(sf.obj);
                const auto writeTime = fs::last_write_time(objPath, ec);
                if (!ec && writeTime > sf.startFile) {
                    sec = std::chrono::duration<float>(writeTime - sf.startFile).count();
                } else {
                    const auto nextSteady = (i + 1 < startedFiles.size()) ? startedFiles[i + 1].startSteady : compileEndSteady;
                    sec = std::chrono::duration<float>(nextSteady - sf.startSteady).count();
                }
                timings.files.push_back({std::move(name), std::max(0.0f, sec)});
            }
        }

        {
            std::lock_guard lock(m_mutex);
            m_end = Clock::now();
            timings.total = std::chrono::duration<float>(m_end - m_start).count();
            m_timings = std::move(timings);
        }
        m_state = ok ? State::Succeeded : State::Failed;
        m_finished = true;
    });
    return true;
}

bool ScriptBuilder::Poll() {
    if (!m_finished.exchange(false)) return false;
    if (m_thread.joinable()) m_thread.join();
    return true;
}

void ScriptBuilder::Append(const std::string& text) {
    std::lock_guard lock(m_mutex);
    m_log += text;
}

std::string ScriptBuilder::Log() const {
    std::lock_guard lock(m_mutex);
    return m_log;
}

float ScriptBuilder::Seconds() const {
    std::lock_guard lock(m_mutex);
    if (m_start == std::chrono::steady_clock::time_point{}) return 0.0f;
    const auto end = m_end == std::chrono::steady_clock::time_point{} ? std::chrono::steady_clock::now() : m_end;
    return std::chrono::duration<float>(end - m_start).count();
}

ScriptBuilder::Timings ScriptBuilder::GetTimings() const {
    std::lock_guard lock(m_mutex);
    Timings t = m_timings;
    if (m_start != std::chrono::steady_clock::time_point{} && m_end == std::chrono::steady_clock::time_point{}) {
        t.total = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_start).count();
    }
    return t;
}

} // namespace editor
