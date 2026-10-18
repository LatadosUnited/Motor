#include "scripts/ScriptBuilder.h"

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
        if (WaitForSingleObject(pi.hProcess, 2) == WAIT_OBJECT_0) {
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
        const fs::path binaryNorm = paths.binaryDir.lexically_normal();
        for (const auto& entry : j) {
            if (!entry.is_object()) continue;
            std::string output = entry.value("output", std::string());
            const std::string file = entry.value("file", std::string());
            if (output.empty() || file.empty()) continue;
            std::replace(output.begin(), output.end(), '\\', '/');
            const fs::path filePath = motor::PathFromUtf8(file).lexically_normal();
            const fs::path rel = filePath.lexically_relative(scriptsNorm);
            std::string display;
            if (!rel.empty() && rel.native()[0] != '.') {
                display = motor::ToGenericUtf8(rel);
            } else {
                display = motor::ToUtf8(filePath.filename());
            }
            const fs::path outPath = motor::PathFromUtf8(output).lexically_normal();
            const fs::path outRel = outPath.lexically_relative(binaryNorm);
            if (!outRel.empty() && !motor::ToGenericUtf8(outRel).starts_with("..")) {
                names[motor::ToGenericUtf8(outRel)] = display;
            }
            names[std::move(output)] = std::move(display);
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

struct NinjaLogEntry {
    int64_t startMs = 0;
    int64_t endMs = 0;
    std::string mtime;
    std::string output;
};

std::vector<NinjaLogEntry> ReadNinjaLog(const fs::path& binaryDir) {
    std::vector<NinjaLogEntry> entries;
    std::error_code ec;
    const fs::path logPath = binaryDir / ".ninja_log";
    if (!fs::is_regular_file(logPath, ec)) return entries;
    std::ifstream in(logPath, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        NinjaLogEntry e;
        if (ss >> e.startMs >> e.endMs >> e.mtime >> e.output) {
            std::replace(e.output.begin(), e.output.end(), '\\', '/');
            entries.push_back(std::move(e));
        }
    }
    return entries;
}

// Nome do arquivo de um objeto compilado ("Girar.cpp"), pelo mapa do CMake ou pelo próprio nome do .obj.
std::string ObjectName(const std::unordered_map<std::string, std::string>& objNames, const std::string& obj) {
    if (auto it = objNames.find(obj); it != objNames.end()) return it->second;
    return FallbackObjectName(obj);
}

// Tempos de uma compilação (configurar, compilar cada arquivo, ligar), tirados da saída do CMake/Ninja linha a
// linha e, no Ninja, do .ninja_log (início e fim exatos de cada saída).
class BuildTimer {
public:
    using Clock = std::chrono::steady_clock;

    BuildTimer(const ScriptProject::Paths& paths, bool isNinja, ScriptBuilder::Timings& timings)
        : m_paths(paths), m_isNinja(isNinja), m_timings(timings) {
        if (m_isNinja) {
            for (const NinjaLogEntry& e : ReadNinjaLog(paths.binaryDir)) m_ninjaBefore[e.output] = e.mtime;
        }
        m_buildStart = Clock::now();
    }

    void OnLine(std::string line) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto now = Clock::now();
        if (line.find("Re-running CMake...") != std::string::npos ||
            (m_reconfigStart == Clock::time_point{} && m_compileStart == Clock::time_point{} &&
             m_linkStart == Clock::time_point{} && line.rfind("-- ", 0) == 0)) {
            if (m_reconfigStart == Clock::time_point{}) m_reconfigStart = now;
        }
        if (line.rfind("-- Build files have been written to:", 0) == 0 && m_reconfigStart != Clock::time_point{}) {
            m_timings.configure += std::chrono::duration<float>(now - m_reconfigStart).count();
            m_reconfigEnd = now;
            m_reconfigStart = {};
        }
        constexpr std::string_view kBuilding = "Building CXX object ";
        constexpr std::string_view kCompiling = "Compiling object ";
        constexpr std::string_view kFromSource = " from source ";
        if (const size_t pos = line.find(kBuilding); pos != std::string::npos) {
            RecordObj(line.substr(pos + kBuilding.size()), now);
        } else if (const size_t pos2 = line.find(kCompiling); pos2 != std::string::npos) {
            const size_t objStart = pos2 + kCompiling.size();
            const size_t fromPos = line.find(kFromSource, objStart);
            if (fromPos != std::string::npos) {
                RecordObj(line.substr(objStart, fromPos - objStart), now);
            }
        }
        if (line.find("Linking CXX shared library ") != std::string::npos ||
            line.find("Linking CXX shared module ") != std::string::npos) {
            if (!m_isNinja && m_compileStart != Clock::time_point{} && m_timings.compile == 0.0f) {
                m_timings.compile = std::chrono::duration<float>(now - m_compileStart).count();
            }
            if (m_linkStart == Clock::time_point{}) m_linkStart = now;
            m_linkEnd = now;
        }
        if (line.find("Built target ") != std::string::npos) {
            if (m_linkStart != Clock::time_point{} && m_timings.link == 0.0f) {
                m_timings.link = std::chrono::duration<float>(now - m_linkStart).count();
            }
        }
    }

    // Fim da compilação: distribui os tempos por arquivo e fecha compilar e ligar.
    void Finish() {
        m_buildEnd = Clock::now();
        if (m_reconfigStart != Clock::time_point{}) {
            m_timings.configure += std::chrono::duration<float>(m_buildEnd - m_reconfigStart).count();
            m_reconfigEnd = m_buildEnd;
        }
        m_phaseStart = m_reconfigEnd != Clock::time_point{} ? m_reconfigEnd : m_buildStart;
        const auto objNames = LoadObjectNames(m_paths);
        if (m_isNinja) FinishNinja(objNames);
        else FinishMakefiles(objNames);
    }

private:
    struct StartedFile {
        std::string obj;
        Clock::time_point eventSteady{};
        fs::file_time_type startFile{};
    };

    void RecordObj(std::string obj, Clock::time_point now) {
        while (!obj.empty() && (obj.back() == ' ' || obj.back() == '\t')) obj.pop_back();
        std::replace(obj.begin(), obj.end(), '\\', '/');
        if (m_compileStart == Clock::time_point{}) m_compileStart = now;
        m_lastObjSteady = now;
        m_startedFiles.push_back({std::move(obj), now, fs::file_time_type::clock::now()});
    }

    // Ninja: o .ninja_log tem início e fim (ms) de cada saída que mudou nesta compilação.
    void FinishNinja(const std::unordered_map<std::string, std::string>& objNames) {
        std::unordered_map<std::string, NinjaLogEntry> changed;
        for (const NinjaLogEntry& e : ReadNinjaLog(m_paths.binaryDir)) {
            auto it = m_ninjaBefore.find(e.output);
            if (it == m_ninjaBefore.end() || it->second != e.mtime) {
                changed[e.output] = e;
            }
        }
        bool usedNinjaLog = false;
        int64_t minObjStart = INT64_MAX;
        int64_t maxObjEnd = 0;
        for (const StartedFile& sf : m_startedFiles) {
            float sec = 0.0f;
            if (auto it = changed.find(sf.obj); it != changed.end()) {
                sec = static_cast<float>(std::max<int64_t>(1, it->second.endMs - it->second.startMs)) / 1000.0f;
                minObjStart = std::min(minObjStart, it->second.startMs);
                maxObjEnd = std::max(maxObjEnd, it->second.endMs);
                usedNinjaLog = true;
            } else {
                sec = std::max(0.001f, std::chrono::duration<float>(sf.eventSteady - m_phaseStart).count());
            }
            m_timings.files.push_back({ObjectName(objNames, sf.obj), sec});
        }
        if (usedNinjaLog && maxObjEnd >= minObjStart) {
            m_timings.compile = static_cast<float>( std::max<int64_t>(1, maxObjEnd - minObjStart)) / 1000.0f;
        } else if (m_lastObjSteady != Clock::time_point{}) {
            m_timings.compile = std::max(0.001f, std::chrono::duration<float>(m_lastObjSteady - m_phaseStart).count());
        }
        const std::string dllName = motor::ToGenericUtf8(m_paths.dll.filename());
        if (auto it = changed.find(dllName); it != changed.end()) {
            m_timings.link = static_cast<float>(std::max<int64_t>(1, it->second.endMs - it->second.startMs)) / 1000.0f;
        } else if (m_linkEnd != Clock::time_point{}) {
            const auto lStart = m_lastObjSteady != Clock::time_point{} ? m_lastObjSteady : m_phaseStart;
            m_timings.link = std::max(0.001f, std::chrono::duration<float>(m_linkEnd - lStart).count());
        }
    }

    // Makefiles: sem log com tempos; cada arquivo vai do aviso "Building" até o .obj ser gravado.
    void FinishMakefiles(const std::unordered_map<std::string, std::string>& objNames) {
        if (m_compileStart != Clock::time_point{} && m_timings.compile == 0.0f) {
            const auto end = m_linkStart != Clock::time_point{} ? m_linkStart : m_buildEnd;
            m_timings.compile = std::chrono::duration<float>(end - m_compileStart).count();
        }
        if (m_linkStart != Clock::time_point{} && m_timings.link == 0.0f) {
            m_timings.link = std::chrono::duration<float>(m_buildEnd - m_linkStart).count();
        }
        const auto compileEndSteady = m_linkStart != Clock::time_point{} ? m_linkStart : m_buildEnd;
        for (size_t i = 0; i < m_startedFiles.size(); ++i) {
            const StartedFile& sf = m_startedFiles[i];
            float sec = 0.0f;
            std::error_code ec;
            const fs::path objPath = m_paths.binaryDir / motor::PathFromUtf8(sf.obj);
            const auto writeTime = fs::last_write_time(objPath, ec);
            if (!ec && writeTime > sf.startFile) {
                sec = std::chrono::duration<float>(writeTime - sf.startFile).count();
            } else {
                const auto nextSteady = (i + 1 < m_startedFiles.size()) ? m_startedFiles[i + 1].eventSteady : compileEndSteady;
                sec = std::chrono::duration<float>(nextSteady - sf.eventSteady).count();
            }
            m_timings.files.push_back({ObjectName(objNames, sf.obj), std::max(0.0f, sec)});
        }
    }

    const ScriptProject::Paths& m_paths;
    const bool m_isNinja;
    ScriptBuilder::Timings& m_timings;
    std::unordered_map<std::string, std::string> m_ninjaBefore;  // saída -> mtime antes de compilar
    std::vector<StartedFile> m_startedFiles;
    Clock::time_point m_buildStart{};
    Clock::time_point m_reconfigStart{};
    Clock::time_point m_reconfigEnd{};
    Clock::time_point m_compileStart{};
    Clock::time_point m_lastObjSteady{};
    Clock::time_point m_linkStart{};
    Clock::time_point m_linkEnd{};
    Clock::time_point m_buildEnd{};
    Clock::time_point m_phaseStart{};
};
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
            BuildTimer timer(paths, toolchain.generator.find("Ninja") != std::string::npos, timings);
            ok = run(ScriptProject::BuildCommand(paths, toolchain), [&](const std::string& chunk) {
                std::istringstream in(chunk);
                std::string line;
                while (std::getline(in, line)) timer.OnLine(std::move(line));
            });
            timer.Finish();
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
