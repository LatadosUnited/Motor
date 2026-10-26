#include "Platform.h"

#include <HubCore.h>

#include <cstdio>
#include <format>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace distrib::platform {

using hub::PathFromUtf8;
using hub::ToUtf8;

#ifdef _WIN32
namespace {
constexpr const wchar_t* kUninstallRoot = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\";
constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring Wide(const std::string& utf8) { return PathFromUtf8(utf8).wstring(); }

// COM no thread atual enquanto o objeto existir (atalhos).
struct ComScope {
    bool uninit = false;
    ComScope() { uninit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)); }
    ~ComScope() {
        if (uninit) CoUninitialize();
    }
};
} // namespace

fs::path KnownFolder(Folder folder) {
    if (folder == Folder::Temp) {
        wchar_t buffer[MAX_PATH + 1] = {};
        const DWORD n = GetTempPathW(MAX_PATH + 1, buffer);
        return n ? fs::path(std::wstring(buffer, n)) : fs::path();
    }
    REFKNOWNFOLDERID id = folder == Folder::LocalAppData        ? FOLDERID_LocalAppData
                          : folder == Folder::StartMenuPrograms ? FOLDERID_Programs
                                                                : FOLDERID_Desktop;
    PWSTR raw = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw))) out = fs::path(raw);
    CoTaskMemFree(raw);
    if (out.empty() && folder == Folder::LocalAppData) {
        if (const wchar_t* env = _wgetenv(L"LOCALAPPDATA")) out = env;
    }
    return out;
}

fs::path ExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n < buffer.size()) {
            buffer.resize(n);
            return fs::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
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

std::string FileVersion(const fs::path& exe) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &ignored);
    if (size == 0) return {};
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(exe.c_str(), 0, size, data.data())) return {};
    VS_FIXEDFILEINFO* info = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &len) || !info) return {};
    return std::format("{}.{}.{}", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                       HIWORD(info->dwFileVersionLS));
}

bool CreateShortcut(const Shortcut& s, std::string* error) {
    ComScope com;
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (SUCCEEDED(hr)) {
        link->SetPath(s.target.c_str());
        link->SetArguments(Wide(s.arguments).c_str());
        link->SetWorkingDirectory(s.workingDir.c_str());
        link->SetDescription(Wide(s.description).c_str());
        link->SetIconLocation(s.target.c_str(), 0);
        IPersistFile* file = nullptr;
        hr = link->QueryInterface(IID_PPV_ARGS(&file));
        if (SUCCEEDED(hr)) {
            std::error_code ec;
            fs::create_directories(s.link.parent_path(), ec);
            hr = file->Save(s.link.c_str(), TRUE);
            file->Release();
        }
        link->Release();
    }
    if (FAILED(hr)) {
        if (error) *error = std::format("Não foi possível criar o atalho '{}' (0x{:08X}).", ToUtf8(s.link),
                                        static_cast<unsigned>(hr));
        return false;
    }
    return true;
}

bool WriteUninstallKey(const std::string& key, const std::vector<RegistryValue>& values, std::string* error) {
    HKEY handle = nullptr;
    const std::wstring path = kUninstallRoot + Wide(key);
    LSTATUS st = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &handle,
                                 nullptr);
    for (const RegistryValue& v : values) {
        if (st != ERROR_SUCCESS) break;
        const std::wstring name = Wide(v.name);
        if (v.isNumber) {
            const DWORD n = v.number;
            st = RegSetValueExW(handle, name.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&n), sizeof(n));
        } else {
            const std::wstring text = Wide(v.text);
            st = RegSetValueExW(handle, name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()),
                                static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)));
        }
    }
    if (handle) RegCloseKey(handle);
    if (st != ERROR_SUCCESS) {
        if (error) *error = std::format("Não foi possível registrar a desinstalação (erro {}).", st);
        return false;
    }
    return true;
}

bool DeleteUninstallKey(const std::string& key) {
    if (key.empty()) return true;
    const std::wstring path = kUninstallRoot + Wide(key);
    const LSTATUS st = RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    return st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND;
}

bool UninstallKeyExists(const std::string& key) {
    HKEY handle = nullptr;
    const std::wstring path = kUninstallRoot + Wide(key);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &handle) != ERROR_SUCCESS) return false;
    RegCloseKey(handle);
    return true;
}

bool WriteStartupValue(const std::string& name, const std::string& command, std::string* error) {
    const std::wstring value = Wide(command);
    const LSTATUS st = RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, Wide(name).c_str(), REG_SZ, value.c_str(),
                                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    if (st != ERROR_SUCCESS) {
        if (error) *error = std::format("Não foi possível pôr o Hub para iniciar com o Windows (erro {}).", st);
        return false;
    }
    return true;
}

bool DeleteStartupValue(const std::string& name) {
    if (name.empty()) return true;
    const LSTATUS st = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, Wide(name).c_str());
    return st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND;
}

std::optional<std::string> ReadStartupValue(const std::string& name) {
    const std::wstring wname = Wide(name);
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, wname.c_str(), RRF_RT_REG_SZ, nullptr, nullptr, &bytes) !=
        ERROR_SUCCESS) {
        return std::nullopt;
    }
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, wname.c_str(), RRF_RT_REG_SZ, nullptr, value.data(), &bytes) !=
        ERROR_SUCCESS) {
        return std::nullopt;
    }
    value.resize(wcsnlen(value.c_str(), value.size()));  // o tamanho lido conta o \0 do fim
    return ToUtf8(fs::path(value));
}

void OpenUrl(const std::string& url) {
    ShellExecuteW(nullptr, L"open", Wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::vector<std::string> CommandLineArgs() {
    // argv no Windows vem na página de código ANSI; a versão wide não perde acentos.
    std::vector<std::string> args;
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; wide && i < count; ++i) args.push_back(ToUtf8(fs::path(wide[i])));
    LocalFree(wide);
    return args;
}

void AttachParentConsole() {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((out == nullptr || out == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::FILE* ignored = nullptr;
        freopen_s(&ignored, "CONOUT$", "w", stdout);
        freopen_s(&ignored, "CONOUT$", "w", stderr);
    }
    SetConsoleOutputCP(CP_UTF8);
}
#else
fs::path KnownFolder(Folder) { return {}; }
fs::path ExecutablePath() { return {}; }
bool MoveToTrash(const fs::path&, std::string* error) {
    if (error) *error = "Lixeira só no Windows.";
    return false;
}
std::string FileVersion(const fs::path&) { return {}; }
bool CreateShortcut(const Shortcut&, std::string* error) {
    if (error) *error = "Atalhos só no Windows.";
    return false;
}
bool WriteUninstallKey(const std::string&, const std::vector<RegistryValue>&, std::string* error) {
    if (error) *error = "Registro só no Windows.";
    return false;
}
bool DeleteUninstallKey(const std::string&) { return true; }
bool UninstallKeyExists(const std::string&) { return false; }
bool WriteStartupValue(const std::string&, const std::string&, std::string* error) {
    if (error) *error = "Registro só no Windows.";
    return false;
}
bool DeleteStartupValue(const std::string&) { return true; }
std::optional<std::string> ReadStartupValue(const std::string&) { return std::nullopt; }
void OpenUrl(const std::string&) {}
std::vector<std::string> CommandLineArgs() { return {}; }
void AttachParentConsole() {}
#endif

} // namespace distrib::platform
