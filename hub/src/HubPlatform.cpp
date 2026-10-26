#include "HubPlatform.h"

#include "HubCore.h"

#include <cstdio>
#include <cstdlib>
#include <format>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace hub::platform {

#ifdef _WIN32
namespace {
fs::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw))) out = fs::path(raw);
    CoTaskMemFree(raw);
    return out;
}
} // namespace

Paths DefaultPaths(const fs::path& dataOverride) {
    Paths p;
    if (!dataOverride.empty()) {
        std::error_code ec;
        p.dataDir = fs::absolute(dataOverride, ec).lexically_normal();  // "C:/x" vira "C:\x" (mensagens e MCP)
    } else {
        fs::path local = KnownFolder(FOLDERID_LocalAppData);
        if (local.empty()) {
            if (const wchar_t* env = _wgetenv(L"LOCALAPPDATA")) local = env;  // versão wide: a ANSI estraga acentos
        }
        p.dataDir = (local.empty() ? fs::current_path() : local) / "Motor";
    }
    p.configFile = p.dataDir / "hub.json";
    p.versionsDir = p.dataDir / PathFromUtf8("Versões");
    p.mcpTokenFile = p.dataDir / kHubMcpTokenFile;
    fs::path documents = KnownFolder(FOLDERID_Documents);
    if (documents.empty()) documents = p.dataDir.parent_path();
    p.defaultProjectsDir = documents / "Motor";
    return p;
}

bool LaunchProcess(const fs::path& exe, const std::vector<std::string>& args, std::string* error) {
    std::wstring commandLine = BuildCommandLine(exe, args);
    const std::wstring workDir = exe.parent_path().wstring();
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, workDir.c_str(), &si, &pi)) {
        if (error) *error = std::format("Não foi possível abrir '{}' (erro {}).", ToUtf8(exe), GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

ChildProcess::~ChildProcess() {
    if (m_handle) CloseHandle(static_cast<HANDLE>(m_handle));  // o processo continua: só deixamos de acompanhar
}

ChildProcess::ChildProcess(ChildProcess&& other) noexcept : m_handle(other.m_handle), m_pid(other.m_pid) {
    other.m_handle = nullptr;
    other.m_pid = 0;
}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
    if (this != &other) {
        if (m_handle) CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = other.m_handle;
        m_pid = other.m_pid;
        other.m_handle = nullptr;
        other.m_pid = 0;
    }
    return *this;
}

std::optional<ChildProcess> ChildProcess::Launch(const fs::path& exe, const std::vector<std::string>& args,
                                                 std::string* error) {
    std::wstring commandLine = BuildCommandLine(exe, args);
    const std::wstring workDir = exe.parent_path().wstring();
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, workDir.c_str(), &si, &pi)) {
        if (error) *error = std::format("Não foi possível abrir '{}' (erro {}).", ToUtf8(exe), GetLastError());
        return std::nullopt;
    }
    CloseHandle(pi.hThread);
    ChildProcess process;
    process.m_handle = pi.hProcess;
    process.m_pid = pi.dwProcessId;
    return process;
}

bool ChildProcess::Running() const {
    return m_handle && WaitForSingleObject(static_cast<HANDLE>(m_handle), 0) == WAIT_TIMEOUT;
}

bool ChildProcess::RequestClose() const {
    struct Search {
        DWORD pid;
        bool posted = false;
    } search{m_pid};
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto* s = reinterpret_cast<Search*>(param);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            // Só as janelas visíveis sem dono: a principal (não as auxiliares do GLFW e do IME).
            if (pid == s->pid && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                s->posted = true;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.posted;
}

bool ChildProcess::Terminate() const {
    return m_handle && TerminateProcess(static_cast<HANDLE>(m_handle), 1);
}

void RevealInExplorer(const fs::path& path) {
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else {
        const std::wstring args = L"/select,\"" + path.wstring() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }
}

bool MoveToTrash(const fs::path& path, std::string* error) {
    std::error_code ec;
    std::wstring from = fs::absolute(path, ec).wstring();
    from.push_back(L'\0');  // pFrom é uma lista terminada por dois '\0'

    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    const int result = SHFileOperationW(&op);
    if (result != 0 || op.fAnyOperationsAborted) {
        if (error) *error = std::format("Não foi possível enviar '{}' para a Lixeira (código {}).", ToUtf8(path), result);
        return false;
    }
    return true;
}

std::optional<fs::path> PickFolder(const fs::path& initialDir, const wchar_t* title) {
    // O seletor de pastas moderno é um objeto COM.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool mustUninit = SUCCEEDED(init);

    std::optional<fs::path> result;
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(title);
        std::error_code ec;
        if (!initialDir.empty() && fs::is_directory(initialDir, ec)) {
            IShellItem* folder = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(initialDir.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                dialog->SetFolder(folder);
                folder->Release();
            }
        }
        if (SUCCEEDED(dialog->Show(GetActiveWindow()))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR chosen = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &chosen))) {
                    result = fs::path(chosen);
                    CoTaskMemFree(chosen);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (mustUninit) CoUninitialize();
    return result;
}

std::vector<std::string> CommandLineArgs() {
    // argv no Windows vem na página de código ANSI; pegamos a versão wide para não perder acentos.
    std::vector<std::string> args;
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; wide && i < count; ++i) args.push_back(ToUtf8(fs::path(wide[i])));
    LocalFree(wide);
    return args;
}

void AttachParentConsole() {
    // O hub é um programa de janela: sem console próprio. Com a saída redirecionada (arquivo, pipe), ela já
    // funciona; senão, escreve no console de quem chamou, se houver.
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((out == nullptr || out == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::FILE* ignored = nullptr;
        freopen_s(&ignored, "CONOUT$", "w", stdout);
        freopen_s(&ignored, "CONOUT$", "w", stderr);
    }
    SetConsoleOutputCP(CP_UTF8);
}
#else
Paths DefaultPaths(const fs::path& dataOverride) {
    Paths p;
    const char* home = std::getenv("HOME");
    p.dataDir = !dataOverride.empty() ? dataOverride : fs::path(home ? home : ".") / ".local" / "share" / "motor";
    p.configFile = p.dataDir / "hub.json";
    p.versionsDir = p.dataDir / PathFromUtf8("Versões");
    p.mcpTokenFile = p.dataDir / kHubMcpTokenFile;
    p.defaultProjectsDir = fs::path(home ? home : ".") / "Motor";
    return p;
}
bool LaunchProcess(const fs::path&, const std::vector<std::string>&, std::string* error) {
    if (error) *error = "Abrir o editor só é suportado no Windows.";
    return false;
}
ChildProcess::~ChildProcess() = default;
ChildProcess::ChildProcess(ChildProcess&&) noexcept = default;
ChildProcess& ChildProcess::operator=(ChildProcess&&) noexcept = default;
std::optional<ChildProcess> ChildProcess::Launch(const fs::path&, const std::vector<std::string>&, std::string* error) {
    if (error) *error = "Abrir o editor só é suportado no Windows.";
    return std::nullopt;
}
bool ChildProcess::Running() const { return false; }
bool ChildProcess::RequestClose() const { return false; }
bool ChildProcess::Terminate() const { return false; }
void RevealInExplorer(const fs::path&) {}
bool MoveToTrash(const fs::path&, std::string* error) {
    if (error) *error = "Lixeira só no Windows.";
    return false;
}
std::optional<fs::path> PickFolder(const fs::path&, const wchar_t*) { return std::nullopt; }
std::vector<std::string> CommandLineArgs() { return {}; }
void AttachParentConsole() {}
#endif

} // namespace hub::platform
