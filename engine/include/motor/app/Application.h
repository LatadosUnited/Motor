#pragma once

#include "motor/core/Window.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace motor {

class Renderer;
class ImGuiLayer;

struct ApplicationDesc {
    WindowDesc window;
    double fixedTimestep = 1.0 / 60.0;  // passo da simulação (física, lógica determinística)
    // Passos fixos no máximo por quadro (FixedStepClock). Com quadros mais longos que fixedTimestep * maxFixedSteps
    // o jogo fica mais lento (câmera lenta), em vez de gastar cada vez mais passos e deixar o quadro cada vez mais
    // lento. 0 = sem limite.
    int maxFixedSteps = 4;
    bool enableImGui = false;
    bool statsInTitle = true;  // FPS e estatísticas no título da janela (o jogo exportado mostra só o nome)
};

// Ciclo de vida:
//   OnInit -> [ OnFixedUpdate* -> OnUpdate -> OnRender -> OnImGui ]* -> OnShutdown
class Application {
public:
    explicit Application(const ApplicationDesc& desc = {});
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Run();
    void Quit();  // encerra sem passar por OnCloseRequested

    // Título base da janela; a Application acrescenta FPS e estatísticas (com statsInTitle).
    void SetWindowTitle(const std::string& title);

    Window& GetWindow() { return *m_window; }
    Renderer& GetRenderer() { return *m_renderer; }
    ImGuiLayer* GetImGui() { return m_imgui.get(); }  // nulo se enableImGui == false

    double ElapsedTime() const { return m_elapsed; }  // segundos desde o início do Run()
    float FramesPerSecond() const { return m_fps; }
    // Quanto do próximo passo fixo já passou (0 a 1), válido no OnUpdate e no OnRender: para interpolar o que o
    // passo fixo mexe (SceneRuntime::SetFixedAlpha).
    float FixedAlpha() const { return m_fixedAlpha; }
    // Limite de quadros por segundo (0 = sem limite além do vsync). O editor usa um limite baixo em segundo
    // plano para não roubar processador de quem compila os scripts.
    void SetFrameLimit(double fps) { m_frameLimit = fps; }
    // Espera ociosa (0 = desligada): sem entrada do usuário há kIdleGrace, cada quadro dorme até o próximo evento
    // (no máximo `seconds`) em vez de desenhar na taxa do monitor. O editor parado não precisa de 60 FPS.
    void SetIdleWait(double seconds) { m_idleWait = seconds; }
    // Conta como entrada do usuário: o que muda a tela sem evento da janela (ex.: um pedido do MCP) mantém os
    // próximos quadros na velocidade normal.
    void NoteActivity();
    static constexpr double kIdleGrace = 0.5;  // segundos depois da última entrada até começar a esperar

protected:
    virtual void OnInit() {}
    virtual void OnFixedUpdate(float /*dt*/) {}
    virtual void OnUpdate(float /*dt*/) {}
    virtual void OnRender(Renderer& /*renderer*/) {}
    virtual void OnImGui() {}  // só chamado com enableImGui
    virtual void OnShutdown() {}
    // Chamado quando o usuário fecha a janela. Retorne false para cancelar (ex.: alterações não salvas).
    virtual bool OnCloseRequested() { return true; }

private:
    ApplicationDesc m_desc;
    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<ImGuiLayer> m_imgui;
    double m_elapsed = 0.0;
    float m_fps = 0.0f;
    float m_fixedAlpha = 1.0f;
    double m_frameLimit = 0.0;
    double m_idleWait = 0.0;
    std::chrono::steady_clock::time_point m_lastActivity{};  // última entrada do usuário
    uint64_t m_lastEventCount = 0;
    int m_lastWidth = 0;
    int m_lastHeight = 0;
    bool m_running = false;
};

} // namespace motor
