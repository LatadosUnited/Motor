#include "motor/app/Application.h"
#include "motor/app/FixedStepClock.h"

#include "motor/core/ImGuiLayer.h"
#include "motor/core/Input.h"
#include "motor/core/Log.h"
#include "motor/core/FrameStats.h"
#include "motor/renderer/GpuMemory.h"
#include "motor/renderer/Renderer.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <thread>

namespace motor {

namespace {
// Memória de vídeo por tipo de recurso nos contadores do quadro (Fase 22), em MB. Todo quadro, mesmo minimizada.
void PublishGpuMemory() {
    const GpuMemoryUsage usage = CurrentGpuMemory();
    FrameStats& stats = FrameStats::Instance();
    constexpr double kMb = 1024.0 * 1024.0;
    for (int k = 0; k < kGpuMemoryKinds; ++k) {
        const std::string name = std::string("vram ") + GpuMemoryKindName(static_cast<GpuMemoryKind>(k)) + " MB";
        stats.SetValue(name, static_cast<double>(usage.bytes[k]) / kMb);
    }
    stats.SetValue("vram total MB", static_cast<double>(usage.Total()) / kMb);
}
} // namespace

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

void Application::NoteActivity() {
    m_lastActivity = std::chrono::steady_clock::now();
}

void Application::SetWindowTitle(const std::string& title) {
    m_desc.window.title = title;
    m_window->SetTitle(title);
}

void Application::Run() {
    using Clock = std::chrono::steady_clock;

    OnInit();

    m_running = true;
    FixedStepClock fixedClock(m_desc.fixedTimestep, m_desc.maxFixedSteps);
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
            if (m_idleWait > 0.0 && frameStart - m_lastActivity > std::chrono::duration<double>(kIdleGrace)) {
                m_window->WaitEvents(m_idleWait);
            } else {
                m_window->PollEvents();
            }
        }

        const auto now = Clock::now();
        // Limita o dt para evitar a "espiral da morte" após travamentos/breakpoints.
        const double realDt = std::chrono::duration<double>(now - last).count();
        double dt = std::min(realDt, 0.25);
        last = now;
        m_elapsed += dt;
        // Entrada do usuário (ou a janela mudou de tamanho): os próximos quadros não esperam.
        if (Input::EventCount() != m_lastEventCount || Input::IsAnyHeld() || m_window->Width() != m_lastWidth ||
            m_window->Height() != m_lastHeight) {
            m_lastEventCount = Input::EventCount();
            m_lastWidth = m_window->Width();
            m_lastHeight = m_window->Height();
            m_lastActivity = now;
        }

        const int fixedSteps = fixedClock.Advance(dt);  // acima de maxFixedSteps, dt diminui (jogo mais lento)
        for (int i = 0; i < fixedSteps; ++i) {
            MOTOR_PROFILE_STAT("FixedUpdate");
            OnFixedUpdate(static_cast<float>(fixedClock.Step()));
        }
        m_fixedAlpha = fixedClock.Alpha();
        FrameStats::Instance().SetValue("passos fixos", fixedSteps);

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
                {
                    MOTOR_PROFILE_STAT("ImGui: começar");
                    m_imgui->BeginFrame();
                }
                OnImGui();
                MOTOR_PROFILE_STAT("ImGui: desenhar");
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
        PublishGpuMemory();
        // Período do quadro: de um fim de volta ao outro (tudo entra, até a espera do vsync).
        const auto frameEnd = Clock::now();
        FrameStats::Instance().EndFrame(std::chrono::duration<double, std::milli>(frameEnd - lastFrameEnd).count());
        lastFrameEnd = frameEnd;

        ++fpsFrames;
        fpsTimer += realDt;  // o relógio de verdade: o dt pode ter sido encurtado pelo passo fixo
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
