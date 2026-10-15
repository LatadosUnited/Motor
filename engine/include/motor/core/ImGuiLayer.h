#pragma once

namespace motor {

class Window;

// Integração do Dear ImGui (backends GLFW + OpenGL3), com docking ativado.
class ImGuiLayer {
public:
    explicit ImGuiLayer(Window& window);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void BeginFrame();
    void EndFrame();

    // true quando o ImGui está usando o mouse/teclado (o jogo deve ignorá-los).
    bool WantsMouse() const;
    bool WantsKeyboard() const;
};

} // namespace motor
