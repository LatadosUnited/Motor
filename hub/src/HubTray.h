#pragma once

// Hub residente: um hub aberto por pasta de dados e o ícone na bandeja do Windows (área de notificação).
//
// A trava (mutex com a InstanceKey) diz se já há um hub aberto; a janela escondida, com a mesma chave no título,
// recebe os pedidos do outro hub ("mostre a janela") e as mensagens do ícone. Ela vive na thread da janela do hub:
// o glfwWaitEvents do loop atende as mensagens dela também.

#include <string>

namespace hub::platform {

class Tray {
public:
    explicit Tray(std::string instanceKey);
    ~Tray();

    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;

    // Fica com a trava e cria a janela escondida. false se outro hub com a mesma chave já está aberto: com
    // `showOther`, ele recebe o pedido de mostrar a janela (o atalho do menu Iniciar traz o hub da bandeja).
    bool Claim(bool showOther);

    bool ShowIcon(const std::string& tooltip);  // ícone na bandeja (clique abre o hub, botão direito: menu)
    void HideIcon();
    bool IconVisible() const { return m_iconVisible; }
    void Notify(const std::string& title, const std::string& text);  // balão do ícone

    // Pedidos que chegaram (outro hub, clique no ícone, menu, Windows encerrando); cada um vale uma vez.
    bool TakeShowRequest();
    bool TakeQuitRequest();

    // Para a janela escondida (WndProc).
    void OnShowRequest() { m_showRequested = true; }
    void OnQuitRequest() { m_quitRequested = true; }
    void OnTrayEvent(unsigned event);
    void OnTaskbarCreated();  // o Explorer reiniciou: o ícone precisa voltar

private:
    std::string m_key;
    void* m_mutex = nullptr;   // HANDLE
    void* m_window = nullptr;  // HWND
    bool m_iconVisible = false;
    std::wstring m_tooltip;
    bool m_showRequested = false;
    bool m_quitRequested = false;
};

} // namespace hub::platform
