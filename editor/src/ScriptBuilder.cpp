#include "ScriptBuilder.h"

#include <motor/core/Log.h>
#include <motor/core/Paths.h>

#include <format>

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
    }
    m_finished = false;
    m_cancel = false;
    m_state = State::Running;

    m_thread = std::thread([this, paths, toolchain] {
        bool ok = true;
        std::string error;
        if (!ScriptProject::WriteBuildFiles(paths, toolchain, &error)) {
            Append(error + "\n");
            ok = false;
        }
        const auto pathDirs = ScriptProject::ToolchainPathDirs(toolchain);
        auto run = [&](const std::vector<std::string>& args) {
            std::string line;
            for (const std::string& a : args) line += (line.empty() ? "" : " ") + a;
            Append("> " + line + "\n");
            const int code = RunProcess(args, pathDirs, [this](const std::string& text) { Append(text); }, m_cancel, &error);
            if (code == -1) Append(error + "\n");
            return code == 0;
        };
        if (ok && ScriptProject::NeedsConfigure(paths)) ok = run(ScriptProject::ConfigureCommand(paths, toolchain));
        if (ok) ok = run(ScriptProject::BuildCommand(paths, toolchain));

        {
            std::lock_guard lock(m_mutex);
            m_end = std::chrono::steady_clock::now();
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

} // namespace editor
