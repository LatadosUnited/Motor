#include "motor/core/Window.h"

#include "motor/core/Input.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace motor {

namespace {
int g_windowCount = 0;

void OnGlfwError(int code, const char* description) {
    log::Error("GLFW [{}]: {}", code, description);
}
} // namespace

Window::Window(const WindowDesc& desc) {
    if (g_windowCount == 0) {
        glfwSetErrorCallback(OnGlfwError);
        if (!glfwInit()) throw std::runtime_error("Falha ao inicializar o GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, desc.msaaSamples);
    glfwWindowHint(GLFW_VISIBLE, desc.visible ? GLFW_TRUE : GLFW_FALSE);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    GLFWmonitor* monitor = desc.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    int width = desc.width;
    int height = desc.height;
    if (monitor && (width <= 0 || height <= 0)) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        width = mode ? mode->width : 1280;
        height = mode ? mode->height : 720;
    }
    if (width <= 0 || height <= 0) {
        width = 1280;
        height = 720;
    }
    m_window = glfwCreateWindow(width, height, desc.title.c_str(), monitor, nullptr);
    if (!m_window) {
        if (g_windowCount == 0) glfwTerminate();
        throw std::runtime_error("Falha ao criar a janela (OpenGL 4.5 é suportado pela sua GPU/driver?)");
    }
    ++g_windowCount;

    // As funções do OpenGL são carregadas pelo Renderer, que é quem usa o contexto.
    glfwMakeContextCurrent(m_window);
    glfwGetFramebufferSize(m_window, &m_width, &m_height);

    // MOTOR_VSYNC=0 desliga o vsync (medição de desempenho: sem ele o FPS fica preso na taxa do monitor).
    const wchar_t* vsyncEnv = _wgetenv(L"MOTOR_VSYNC");
    SetVSync(desc.vsync && !(vsyncEnv && vsyncEnv[0] == L'0'));
    Input::Attach(m_window);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetDropCallback(m_window, OnDrop);

    log::Info("Janela criada: {}x{}", m_width, m_height);
}

Window::~Window() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        if (--g_windowCount == 0) glfwTerminate();
    }
}

bool Window::IsFocused() const {
    return glfwGetWindowAttrib(m_window, GLFW_FOCUSED) != 0;
}

bool Window::ShouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void Window::RequestClose() {
    glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void Window::CancelClose() {
    glfwSetWindowShouldClose(m_window, GLFW_FALSE);
}

void Window::PollEvents() {
    glfwPollEvents();
    Input::PollGamepads();
    glfwGetFramebufferSize(m_window, &m_width, &m_height);
}

void Window::WaitEvents(double timeoutSeconds) {
    glfwWaitEventsTimeout(timeoutSeconds);
    Input::PollGamepads();
    glfwGetFramebufferSize(m_window, &m_width, &m_height);
}

void Window::Wake() {
    glfwPostEmptyEvent();
}

void Window::SwapBuffers() {
    glfwSwapBuffers(m_window);
}

void Window::Show() {
    if (!glfwGetWindowAttrib(m_window, GLFW_VISIBLE)) glfwShowWindow(m_window);
}

void Window::SetTitle(const std::string& title) {
    glfwSetWindowTitle(m_window, title.c_str());
}

void Window::SetVSync(bool enabled) {
    glfwSwapInterval(enabled ? 1 : 0);
}

void Window::SetDisplayMode(bool fullscreen, int width, int height) {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* desktop = monitor ? glfwGetVideoMode(monitor) : nullptr;
    if (fullscreen && monitor) {
        if (width <= 0 || height <= 0) {
            width = desktop ? desktop->width : 1280;
            height = desktop ? desktop->height : 720;
        }
        glfwSetWindowMonitor(m_window, monitor, 0, 0, width, height, GLFW_DONT_CARE);
    } else {
        if (width <= 0 || height <= 0) {
            width = 1280;
            height = 720;
        }
        int x = 100;
        int y = 100;
        if (desktop) {
            int mx = 0;
            int my = 0;
            glfwGetMonitorPos(monitor, &mx, &my);
            x = mx + std::max(0, (desktop->width - width) / 2);
            y = my + std::max(0, (desktop->height - height) / 2);
        }
        glfwSetWindowMonitor(m_window, nullptr, x, y, width, height, GLFW_DONT_CARE);
    }
    glfwGetFramebufferSize(m_window, &m_width, &m_height);
    log::Info("Tela: {} {}x{}", fullscreen ? "cheia" : "janela", m_width, m_height);
}

bool Window::IsFullscreen() const {
    return glfwGetWindowMonitor(m_window) != nullptr;
}

std::vector<VideoMode> Window::MonitorModes() {
    std::vector<VideoMode> modes;
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    int count = 0;
    const GLFWvidmode* list = monitor ? glfwGetVideoModes(monitor, &count) : nullptr;
    for (int i = 0; i < count; ++i) {
        const VideoMode mode{list[i].width, list[i].height};
        if (std::find(modes.begin(), modes.end(), mode) == modes.end()) modes.push_back(mode);
    }
    std::sort(modes.begin(), modes.end(), [](const VideoMode& a, const VideoMode& b) {
        return a.width != b.width ? a.width < b.width : a.height < b.height;
    });
    return modes;
}

VideoMode Window::DesktopMode() {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    return mode ? VideoMode{mode->width, mode->height} : VideoMode{};
}

void Window::SetCursorMode(CursorMode mode) {
    if (mode == m_cursorMode) return;
    m_cursorMode = mode;
    const int glfwMode = mode == CursorMode::Locked   ? GLFW_CURSOR_DISABLED
                         : mode == CursorMode::Hidden ? GLFW_CURSOR_HIDDEN
                                                      : GLFW_CURSOR_NORMAL;
    glfwSetInputMode(m_window, GLFW_CURSOR, glfwMode);
    if (mode == CursorMode::Locked && glfwRawMouseMotionSupported()) {
        glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    Input::IgnoreNextMouseDelta();
}

std::vector<std::filesystem::path> Window::TakeDroppedFiles() {
    return std::exchange(m_droppedFiles, {});
}

void Window::OnDrop(GLFWwindow* window, int count, const char** paths) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (!self) return;
    for (int i = 0; i < count; ++i) self->m_droppedFiles.push_back(PathFromUtf8(paths[i]));  // GLFW entrega UTF-8
}

float Window::AspectRatio() const {
    return m_height > 0 ? static_cast<float>(m_width) / static_cast<float>(m_height) : 1.0f;
}

} // namespace motor
