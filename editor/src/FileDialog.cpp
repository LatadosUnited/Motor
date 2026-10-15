#include "FileDialog.h"

#include <motor/core/Log.h>
#include <motor/core/Paths.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>   // DROPFILES
#include <oleidl.h>   // DROPEFFECT_*

#include <algorithm>
#include <iterator>
#endif

#include <format>

namespace editor {

#ifdef _WIN32
namespace {
struct KindInfo {
    const wchar_t* filter;
    const wchar_t* extension;
};

KindInfo Info(FileDialog::Kind kind) {
    switch (kind) {
        case FileDialog::Kind::Project:
            return {L"Projeto do Motor (*.mproject)\0*.mproject\0Todos os arquivos (*.*)\0*.*\0", L"mproject"};
        case FileDialog::Kind::Scene:
        default:
            return {L"Cena do Motor (*.mscene)\0*.mscene\0Todos os arquivos (*.*)\0*.*\0", L"mscene"};
    }
}

std::optional<std::filesystem::path> RunFileDialog(bool save, FileDialog::Kind kind, const std::filesystem::path& start) {
    wchar_t buffer[4096] = L"";
    if (save && start.has_filename()) {
        const std::wstring name = start.filename().wstring();
        name.copy(buffer, std::min(name.size(), std::size(buffer) - 1));
    }

    std::wstring dir;
    if (!start.empty()) {
        std::error_code ec;
        dir = (std::filesystem::is_directory(start, ec) ? start : start.parent_path()).wstring();
    }

    const KindInfo info = Info(kind);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = info.filter;
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = static_cast<DWORD>(std::size(buffer));
    ofn.lpstrDefExt = info.extension;
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    // NOCHANGEDIR: sem isso o diálogo muda o diretório atual do processo (e o imgui.ini iria junto).
    ofn.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (!ok) {
        if (const DWORD err = CommDlgExtendedError()) motor::log::Error("Diálogo de arquivo falhou (código {})", err);
        return std::nullopt;
    }
    return std::filesystem::path(buffer);
}
} // namespace

std::optional<std::filesystem::path> FileDialog::OpenFile(Kind kind, const std::filesystem::path& initialDir) {
    return RunFileDialog(false, kind, initialDir);
}

std::optional<std::filesystem::path> FileDialog::SaveFile(Kind kind, const std::filesystem::path& suggestedPath) {
    return RunFileDialog(true, kind, suggestedPath);
}

std::optional<std::filesystem::path> FileDialog::PickFolder(const std::filesystem::path& initialDir) {
    // O seletor de pastas moderno é um objeto COM.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool mustUninit = SUCCEEDED(init);

    std::optional<std::filesystem::path> result;
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(L"Escolha a pasta");

        if (!initialDir.empty()) {
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
                    result = std::filesystem::path(chosen);
                    CoTaskMemFree(chosen);
                }
                item->Release();
            }
        }
        dialog->Release();
    } else {
        motor::log::Error("Não foi possível criar o seletor de pastas");
    }

    if (mustUninit) CoUninitialize();
    return result;
}

void Shell::RevealInExplorer(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else {
        const std::wstring args = L"/select,\"" + path.wstring() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }
}

bool Shell::MoveToTrash(const std::filesystem::path& path, std::string* error) {
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);

    // pFrom é uma lista terminada por dois '\0'.
    std::wstring from = absolute.wstring();
    from.push_back(L'\0');

    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

    const int result = SHFileOperationW(&op);
    if (result != 0 || op.fAnyOperationsAborted) {
        if (error) *error = std::format("Não foi possível enviar '{}' para a Lixeira (código {}).",
                                        motor::ToUtf8(path.filename()), result);
        return false;
    }
    return true;
}
void Shell::OpenWithDefaultApp(const std::filesystem::path& path) {
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, path.parent_path().c_str(), SW_SHOWNORMAL));
    if (result <= 32) {  // sem programa associado: mostra no Explorer
        motor::log::Warn("Nenhum programa abre '{}'", motor::ToUtf8(path.filename()));
        RevealInExplorer(path);
    }
}

bool Clipboard::SetFiles(const std::vector<std::filesystem::path>& paths, bool cut) {
    if (paths.empty()) return false;

    // CF_HDROP: DROPFILES seguido da lista de caminhos (wide), cada um terminado em '\0', e um '\0' final.
    std::wstring list;
    for (const auto& p : paths) {
        std::error_code ec;
        list += std::filesystem::absolute(p, ec).wstring();
        list.push_back(L'\0');
    }
    list.push_back(L'\0');

    const size_t bytes = sizeof(DROPFILES) + list.size() * sizeof(wchar_t);
    HGLOBAL drop = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (!drop) return false;
    auto* header = static_cast<DROPFILES*>(GlobalLock(drop));
    header->pFiles = sizeof(DROPFILES);
    header->fWide = TRUE;
    std::copy(list.begin(), list.end(), reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(header) + sizeof(DROPFILES)));
    GlobalUnlock(drop);

    // "Preferred DropEffect" diz ao Explorer se é copiar ou recortar.
    HGLOBAL effect = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (effect) {
        *static_cast<DWORD*>(GlobalLock(effect)) = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
        GlobalUnlock(effect);
    }

    // EmptyClipboard precisa de uma janela dona; a janela ativa é a do editor (o atalho veio dela).
    if (!OpenClipboard(GetActiveWindow())) {
        GlobalFree(drop);
        if (effect) GlobalFree(effect);
        return false;
    }
    EmptyClipboard();
    const bool ok = SetClipboardData(CF_HDROP, drop) != nullptr;
    if (!ok) GlobalFree(drop);
    if (effect && !SetClipboardData(RegisterClipboardFormatW(L"Preferred DropEffect"), effect)) GlobalFree(effect);
    CloseClipboard();
    return ok;
}

std::vector<std::filesystem::path> Clipboard::GetFiles() {
    std::vector<std::filesystem::path> result;
    if (!IsClipboardFormatAvailable(CF_HDROP) || !OpenClipboard(GetActiveWindow())) return result;
    if (HANDLE data = GetClipboardData(CF_HDROP)) {
        if (auto drop = static_cast<HDROP>(GlobalLock(data))) {
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; ++i) {
                std::wstring path(DragQueryFileW(drop, i, nullptr, 0), L'\0');
                DragQueryFileW(drop, i, path.data(), static_cast<UINT>(path.size() + 1));
                result.emplace_back(path);
            }
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return result;
}

unsigned long Clipboard::SequenceNumber() {
    return GetClipboardSequenceNumber();
}
#else
std::optional<std::filesystem::path> FileDialog::OpenFile(Kind, const std::filesystem::path&) {
    motor::log::Warn("Diálogos de arquivo ainda não implementados nesta plataforma");
    return std::nullopt;
}

std::optional<std::filesystem::path> FileDialog::SaveFile(Kind, const std::filesystem::path&) {
    motor::log::Warn("Diálogos de arquivo ainda não implementados nesta plataforma");
    return std::nullopt;
}

std::optional<std::filesystem::path> FileDialog::PickFolder(const std::filesystem::path&) {
    motor::log::Warn("Diálogos de arquivo ainda não implementados nesta plataforma");
    return std::nullopt;
}

void Shell::RevealInExplorer(const std::filesystem::path&) {
    motor::log::Warn("Mostrar no gerenciador de arquivos ainda não implementado nesta plataforma");
}

bool Shell::MoveToTrash(const std::filesystem::path& path, std::string* error) {
    if (error) *error = std::format("Lixeira não suportada nesta plataforma ('{}' não foi removido).",
                                    motor::ToUtf8(path.filename()));
    return false;
}

void Shell::OpenWithDefaultApp(const std::filesystem::path&) {
    motor::log::Warn("Abrir com o programa padrão ainda não implementado nesta plataforma");
}

bool Clipboard::SetFiles(const std::vector<std::filesystem::path>&, bool) { return false; }
std::vector<std::filesystem::path> Clipboard::GetFiles() { return {}; }
unsigned long Clipboard::SequenceNumber() { return 0; }
#endif

} // namespace editor
