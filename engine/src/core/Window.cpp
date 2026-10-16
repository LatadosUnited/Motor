#include "motor/core/Window.h"

#include "motor/core/Input.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"

#include <GLFW/glfw3.h>

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
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    m_window = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), nullptr, nullptr);
    if (!m_window) {
        if (g_windowCount == 0) glfwTerminate();
        throw std::runtime_error("Falha ao criar a janela (OpenGL 4.5 é suportado pela sua GPU/driver?)");
    }
    ++g_windowCount;

    // As funções do OpenGL são carregadas pelo Renderer, que é quem usa o contexto.
    glfwMakeContextCurrent(m_window);
    glfwGetFramebufferSize(m_window, &m_width, &m_height);

    SetVSync(desc.vsync);
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
    glfwGetFramebufferSize(m_window, &m_width, &m_height);
}

void Window::SwapBuffers() {
    glfwSwapBuffers(m_window);
}

void Window::SetTitle(const std::string& title) {
    glfwSetWindowTitle(m_window, title.c_str());
}

void Window::SetVSync(bool enabled) {
    glfwSwapInterval(enabled ? 1 : 0);
}

void Window::SetCursorCaptured(bool captured) {
    if (captured == m_cursorCaptured) return;
    m_cursorCaptured = captured;
    glfwSetInputMode(m_window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (captured && glfwRawMouseMotionSupported()) {
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
