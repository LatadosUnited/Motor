#include "motor/app/Application.h"

#include "motor/core/ImGuiLayer.h"
#include "motor/core/Input.h"
#include "motor/core/Log.h"
#include "motor/core/FrameStats.h"
#include "motor/renderer/Renderer.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <thread>

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

    auto lastFrameEnd = Clock::now();
    while (m_running) {
        const auto frameStart = Clock::now();
        if (m_window->ShouldClose()) {
            if (OnCloseRequested()) break;
            m_window->CancelClose();
        }

        {
            MOTOR_PROFILE_STAT("Eventos");
            Input::BeginFrame();
            m_window->PollEvents();
        }

        const auto now = Clock::now();
        // Limita o dt para evitar a "espiral da morte" após travamentos/breakpoints.
        const double dt = std::min(std::chrono::duration<double>(now - last).count(), 0.25);
        last = now;
        m_elapsed += dt;

        accumulator += dt;
        while (accumulator >= fixedDt) {
            MOTOR_PROFILE_STAT("FixedUpdate");
            OnFixedUpdate(static_cast<float>(fixedDt));
            accumulator -= fixedDt;
        }

        {
            MOTOR_PROFILE_STAT("Update");
            OnUpdate(static_cast<float>(dt));
        }

        if (!m_window->IsMinimized()) {
            {
                MOTOR_PROFILE_STAT("Render");
                m_renderer->BeginFrame(m_window->Width(), m_window->Height());
                OnRender(*m_renderer);
            }
            if (m_imgui) {
                MOTOR_PROFILE_STAT("ImGui");
                m_imgui->BeginFrame();
                OnImGui();
                m_imgui->EndFrame();
            }
            {
                MOTOR_PROFILE_STAT("SwapBuffers");  // inclui a espera do vsync
                m_window->SwapBuffers();
            }
            const RenderStats& rs = m_renderer->GetStats();
            MOTOR_PROFILE_PLOT("Draws", rs.drawCalls);
            MOTOR_PROFILE_PLOT("Triângulos", rs.triangles);
            FrameStats& stats = FrameStats::Instance();
            stats.SetValue("draws", rs.drawCalls);
            stats.SetValue("draws sombra", rs.shadowDrawCalls);
            stats.SetValue("triangulos", rs.triangles);
            stats.SetValue("recortados", rs.culled);
            stats.SetValue("transparentes", rs.transparent);
        } else {
            // Minimizada não há SwapBuffers para esperar o vsync: sem esta pausa o loop gira a milhares de FPS.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (m_frameLimit > 0.0) {
            std::this_thread::sleep_until(frameStart + std::chrono::duration_cast<Clock::duration>(
                                                          std::chrono::duration<double>(1.0 / m_frameLimit)));
        }
        MOTOR_PROFILE_FRAME();
        // Período do quadro: de um fim de volta ao outro (tudo entra, até a espera do vsync).
        const auto frameEnd = Clock::now();
        FrameStats::Instance().EndFrame(std::chrono::duration<double, std::milli>(frameEnd - lastFrameEnd).count());
        lastFrameEnd = frameEnd;

        ++fpsFrames;
        fpsTimer += dt;
        if (fpsTimer >= 1.0) {
            m_fps = static_cast<float>(fpsFrames / fpsTimer);
            const auto& s = m_renderer->GetStats();
            if (m_desc.statsInTitle) {
                m_window->SetTitle(std::format("{} | {:.0f} FPS ({:.2f} ms) | {} draws, {} tris",
                                               m_desc.window.title, m_fps, 1000.0 / m_fps,
                                               s.drawCalls, s.triangles));
            }
            fpsFrames = 0;
            fpsTimer = 0.0;
        }
    }

    OnShutdown();
    log::Info("Encerrando.");
}

} // namespace motor
