#pragma once

#include "motor/core/Input.h"

#include <filesystem>
#include <string>
#include <vector>

struct GLFWwindow;

namespace motor {

struct WindowDesc {
    std::string title = "Motor";
    int width = 1280;
    int height = 720;
    bool vsync = true;
    int msaaSamples = 4;
    bool fullscreen = false;  // no monitor principal, com width x height (0 = a resolução atual do monitor)
    // false: a janela nasce escondida e aparece no Show() (o jogo exportado mostra quando já tem o que desenhar,
    // em vez de uma janela branca enquanto o renderer inicia).
    bool visible = true;
};

// Resolução que o monitor principal aceita em tela cheia.
struct VideoMode {
    int width = 0;
    int height = 0;
    bool operator==(const VideoMode&) const = default;
};

class Window {
public:
    explicit Window(const WindowDesc& desc);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool ShouldClose() const;
    void RequestClose();
    void CancelClose();
    void PollEvents();
    void SwapBuffers();

    void SetTitle(const std::string& title);
    void Show();  // mostra a janela criada com visible = false (sem efeito se já está visível)
    void SetVSync(bool enabled);

    // Tela cheia no monitor principal (width/height 0 = a resolução atual dele) ou janela com esse tamanho
    // (0 = 1280x720), centrada. Volta para janela no tamanho pedido, não no de antes.
    void SetDisplayMode(bool fullscreen, int width, int height);
    bool IsFullscreen() const;
    // Resoluções do monitor principal (sem repetir as que só mudam a taxa), da menor para a maior.
    static std::vector<VideoMode> MonitorModes();
    static VideoMode DesktopMode();  // resolução atual do monitor principal

    void SetCursorMode(CursorMode mode);
    CursorMode GetCursorMode() const { return m_cursorMode; }
    void SetCursorCaptured(bool captured) { SetCursorMode(captured ? CursorMode::Locked : CursorMode::Normal); }
    bool IsCursorCaptured() const { return m_cursorMode == CursorMode::Locked; }

    // Tamanho do framebuffer em pixels (pode diferir do tamanho lógico em telas HiDPI).
    int Width() const { return m_width; }
    int Height() const { return m_height; }
    float AspectRatio() const;
    bool IsMinimized() const { return m_width == 0 || m_height == 0; }
    bool IsFocused() const;  // a janela tem o foco do teclado (outra janela em cima: false)

    GLFWwindow* Native() const { return m_window; }

    // Arquivos/pastas soltos na janela (arrastados do Explorer) desde a última chamada.
    std::vector<std::filesystem::path> TakeDroppedFiles();

private:
    static void OnDrop(GLFWwindow* window, int count, const char** paths);

    GLFWwindow* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
    CursorMode m_cursorMode = CursorMode::Normal;
    std::vector<std::filesystem::path> m_droppedFiles;
};

} // namespace motor
