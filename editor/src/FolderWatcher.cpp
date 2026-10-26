#include "FolderWatcher.h"

#include <motor/core/Log.h>
#include <motor/core/Paths.h>

#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace editor {

FolderWatcher::~FolderWatcher() {
    Close();
}

void FolderWatcher::Close() {
#ifdef _WIN32
    if (m_handle) FindCloseChangeNotification(static_cast<HANDLE>(m_handle));
#endif
    m_handle = nullptr;
}

void FolderWatcher::Watch(const std::filesystem::path& folder) {
    Close();
    m_folder = folder;
    m_first = true;
    if (folder.empty()) return;
#ifdef _WIN32
    constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                              FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;
    HANDLE handle = FindFirstChangeNotificationW(folder.wstring().c_str(), TRUE, kFilter);
    if (handle == INVALID_HANDLE_VALUE) {
        motor::log::Warn("Não foi possível vigiar a pasta '{}': os arquivos do projeto serão conferidos a cada 2 s",
                         motor::ToUtf8(folder));
        return;
    }
    m_handle = handle;
#endif
}

bool FolderWatcher::Changed() {
    if (!m_handle) return true;
    bool changed = std::exchange(m_first, false);
#ifdef _WIN32
    // Esvazia os avisos acumulados: vários arquivos gravados entre duas chamadas contam como uma mudança.
    for (int i = 0; i < 64 && WaitForSingleObject(static_cast<HANDLE>(m_handle), 0) == WAIT_OBJECT_0; ++i) {
        changed = true;
        if (!FindNextChangeNotification(static_cast<HANDLE>(m_handle))) {
            Close();  // a pasta sumiu ou o vigia falhou: volta a conferir sempre
            return true;
        }
    }
#endif
    return changed;
}

} // namespace editor
