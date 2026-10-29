#include "HubTray.h"

#include "HubCore.h"

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

#include <cwchar>

namespace hub::platform {

#ifdef _WIN32
namespace {

constexpr const wchar_t* kWindowClass = L"MotorHubResidente";
constexpr UINT kMsgShow = WM_APP + 1;  // outro hub pediu: mostre a janela
constexpr UINT kMsgTray = WM_APP + 2;  // eventos do ícone (lParam: a mensagem do mouse)
constexpr UINT kIconId = 1;
constexpr UINT kMenuOpen = 1;
constexpr UINT kMenuQuit = 2;

UINT TaskbarCreatedMessage() {
    static const UINT message = RegisterWindowMessageW(L"TaskbarCreated");
    return message;
}

std::wstring WindowTitle(const std::string& key) {
    return L"MotorHub-" + PathFromUtf8(key).wstring();
}

void CopyText(wchar_t* out, size_t capacity, const std::wstring& text) {
    std::wcsncpy(out, text.c_str(), capacity - 1);
    out[capacity - 1] = L'\0';
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* tray = reinterpret_cast<Tray*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (tray) {
        if (msg == kMsgShow) {
            tray->OnShowRequest();
            return 0;
        }
        if (msg == kMsgTray) {
            tray->OnTrayEvent(static_cast<unsigned>(LOWORD(lParam)));
            return 0;
        }
        if (msg == TaskbarCreatedMessage()) {
            tray->OnTaskbarCreated();
            return 0;
        }
        if (msg == WM_ENDSESSION && wParam) {
            tray->OnQuitRequest();  // o Windows está saindo: o hub fecha direito (downloads param, config salva)
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool RegisterWindowClass() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HICON AppIcon() {
    // Sem ícone próprio ainda (Fase 32): o primeiro do .exe, se houver, senão o padrão do Windows.
    HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    // IDI_APPLICATION (32512): a macro é ANSI sem UNICODE.
    return icon ? icon : LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
}

NOTIFYICONDATAW IconData(void* window) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = static_cast<HWND>(window);
    data.uID = kIconId;
    return data;
}

} // namespace

Tray::Tray(std::string instanceKey) : m_key(std::move(instanceKey)) {}

Tray::~Tray() {
    HideIcon();
    if (m_window) DestroyWindow(static_cast<HWND>(m_window));
    if (m_mutex) CloseHandle(static_cast<HANDLE>(m_mutex));
}

bool Tray::Claim(bool showOther) {
    const std::wstring name = L"Local\\MotorHub-" + PathFromUtf8(m_key).wstring();
    HANDLE mutex = CreateMutexW(nullptr, FALSE, name.c_str());
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        if (showOther) {
            // O outro pode ter acabado de abrir e ainda não ter a janela: espera um pouco por ela.
            const std::wstring title = WindowTitle(m_key);
            for (int attempt = 0; attempt < 30; ++attempt) {
                if (HWND other = FindWindowW(kWindowClass, title.c_str())) {
                    DWORD pid = 0;
                    GetWindowThreadProcessId(other, &pid);
                    AllowSetForegroundWindow(pid);  // senão a janela dele só pisca na barra de tarefas
                    PostMessageW(other, kMsgShow, 0, 0);
                    break;
                }
                Sleep(100);
            }
        }
        return false;
    }
    m_mutex = mutex;  // sem trava (erro raro do Windows), o hub abre do mesmo jeito

    if (!RegisterWindowClass()) return true;
    // Janela de topo que nunca aparece: o menu do ícone precisa de uma (as só de mensagens não fecham o menu
    // quando se clica fora) e o FindWindow do outro hub acha pelo título.
    m_window = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, WindowTitle(m_key).c_str(), WS_POPUP, 0, 0, 0, 0,
                               nullptr, nullptr, GetModuleHandleW(nullptr), this);
    return true;
}

bool Tray::ShowIcon(const std::string& tooltip) {
    if (!m_window) return false;
    m_tooltip = PathFromUtf8(tooltip).wstring();
    NOTIFYICONDATAW data = IconData(m_window);
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kMsgTray;
    data.hIcon = AppIcon();
    CopyText(data.szTip, std::size(data.szTip), m_tooltip);
    const DWORD action = m_iconVisible ? NIM_MODIFY : NIM_ADD;
    m_iconVisible = Shell_NotifyIconW(action, &data) != FALSE;
    return m_iconVisible;
}

void Tray::HideIcon() {
    if (!m_iconVisible) return;
    NOTIFYICONDATAW data = IconData(m_window);
    Shell_NotifyIconW(NIM_DELETE, &data);
    m_iconVisible = false;
}

void Tray::Notify(const std::string& title, const std::string& text) {
    if (!m_iconVisible) return;
    NOTIFYICONDATAW data = IconData(m_window);
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_INFO;
    CopyText(data.szInfoTitle, std::size(data.szInfoTitle), PathFromUtf8(title).wstring());
    CopyText(data.szInfo, std::size(data.szInfo), PathFromUtf8(text).wstring());
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void Tray::OnTrayEvent(unsigned event) {
    if (event == WM_LBUTTONUP || event == WM_LBUTTONDBLCLK) {
        m_showRequested = true;
        return;
    }
    if (event != WM_RBUTTONUP && event != WM_CONTEXTMENU) return;

    HWND hwnd = static_cast<HWND>(m_window);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuOpen, L"Abrir o Hub");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"Sair do Hub");
    SetMenuDefaultItem(menu, kMenuOpen, FALSE);
    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(hwnd);  // sem isso o menu não fecha ao clicar fora
    const UINT chosen = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                                         cursor.x, cursor.y, 0, hwnd, nullptr));
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (chosen == kMenuOpen) m_showRequested = true;
    if (chosen == kMenuQuit) m_quitRequested = true;
}

void Tray::OnTaskbarCreated() {
    if (!m_iconVisible) return;
    m_iconVisible = false;
    ShowIcon(ToUtf8(fs::path(m_tooltip)));
}
#else
Tray::Tray(std::string instanceKey) : m_key(std::move(instanceKey)) {}
Tray::~Tray() = default;
bool Tray::Claim(bool) { return true; }
bool Tray::ShowIcon(const std::string&) { return false; }
void Tray::HideIcon() {}
void Tray::Notify(const std::string&, const std::string&) {}
void Tray::OnTrayEvent(unsigned) {}
void Tray::OnTaskbarCreated() {}
#endif

bool Tray::TakeShowRequest() {
    const bool requested = m_showRequested;
    m_showRequested = false;
    return requested;
}

bool Tray::TakeQuitRequest() {
    const bool requested = m_quitRequested;
    m_quitRequested = false;
    return requested;
}

} // namespace hub::platform
