#pragma once

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
    void SetVSync(bool enabled);

    void SetCursorCaptured(bool captured);
    bool IsCursorCaptured() const { return m_cursorCaptured; }

    // Tamanho do framebuffer em pixels (pode diferir do tamanho lógico em telas HiDPI).
    int Width() const { return m_width; }
    int Height() const { return m_height; }
    float AspectRatio() const;
    bool IsMinimized() const { return m_width == 0 || m_height == 0; }

    GLFWwindow* Native() const { return m_window; }

    // Arquivos/pastas soltos na janela (arrastados do Explorer) desde a última chamada.
    std::vector<std::filesystem::path> TakeDroppedFiles();

private:
    static void OnDrop(GLFWwindow* window, int count, const char** paths);

    GLFWwindow* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
    bool m_cursorCaptured = false;
    std::vector<std::filesystem::path> m_droppedFiles;
};

} // namespace motor
