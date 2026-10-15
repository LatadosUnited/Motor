#include "motor/core/Application.h"

#include "motor/core/ImGuiLayer.h"
#include "motor/core/Input.h"
#include "motor/core/Log.h"
#include "motor/renderer/Renderer.h"

#include <algorithm>
#include <chrono>
#include <format>

namespace motor {

Application::Application(const ApplicationDesc& desc) : m_desc(desc) {
    m_window = std::make_unique<Window>(desc.window);
    m_renderer = std::make_unique<Renderer>();
    if (desc.enableImGui) m_imgui = std::make_unique<ImGuiLayer>(*m_window);
}

Application::~Application() {
    // Tudo que usa OpenGL precisa morrer antes do contexto (que pertence à janela).
    m_imgui.reset();
    m_renderer.reset();
    m_window.reset();
}

void Application::Quit() {
    m_running = false;
}

void Application::SetWindowTitle(const std::string& title) {
    m_desc.window.title = title;
    m_window->SetTitle(title);
}

void Application::Run() {
    using Clock = std::chrono::steady_clock;

    OnInit();

    m_running = true;
    const double fixedDt = m_desc.fixedTimestep;
    double accumulator = 0.0;
    auto last = Clock::now();

    double fpsTimer = 0.0;
    int fpsFrames = 0;

    while (m_running) {
        if (m_window->ShouldClose()) {
            if (OnCloseRequested()) break;
            m_window->CancelClose();
        }

        Input::BeginFrame();
        m_window->PollEvents();

        const auto now = Clock::now();
        // Limita o dt para evitar a "espiral da morte" após travamentos/breakpoints.
        const double dt = std::min(std::chrono::duration<double>(now - last).count(), 0.25);
        last = now;
        m_elapsed += dt;

        accumulator += dt;
        while (accumulator >= fixedDt) {
            OnFixedUpdate(static_cast<float>(fixedDt));
            accumulator -= fixedDt;
        }

        OnUpdate(static_cast<float>(dt));

        if (!m_window->IsMinimized()) {
            m_renderer->BeginFrame(m_window->Width(), m_window->Height());
            OnRender(*m_renderer);
            if (m_imgui) {
                m_imgui->BeginFrame();
                OnImGui();
                m_imgui->EndFrame();
            }
            m_window->SwapBuffers();
        }

        ++fpsFrames;
        fpsTimer += dt;
        if (fpsTimer >= 1.0) {
            m_fps = static_cast<float>(fpsFrames / fpsTimer);
            const auto& s = m_renderer->GetStats();
            m_window->SetTitle(std::format("{} | {:.0f} FPS ({:.2f} ms) | {} draws, {} tris",
                                           m_desc.window.title, m_fps, 1000.0 / m_fps,
                                           s.drawCalls, s.triangles));
            fpsFrames = 0;
            fpsTimer = 0.0;
        }
    }

    OnShutdown();
    log::Info("Encerrando.");
}

} // namespace motor
