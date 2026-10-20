#pragma once

#include "EditorContext.h"

#include <motor/Motor.h>

#include <glm/vec2.hpp>
#include <imgui.h>

#include <memory>

namespace editor {

class PlayController;

// Janela "Jogo": a cena pela câmera principal, com o HUD, ao lado do Viewport (que fica na câmera do editor).
// Só existe com Jogo > "Cena e Jogo lado a lado" ligado (PlayController::SideBySide). No Play, o jogo lê o
// teclado e o mouse com esta janela em foco; fora do Play ela mostra como a câmera vê a cena.
class GameView {
public:
    void Init();

    // Logo depois do dockspace, antes das janelas: ao ligar o modo, divide o nó do Viewport e põe o Jogo à
    // direita (se o imgui.ini ainda não tem um lugar para ele).
    void PrepareDock(const PlayController& play);
    void Render(motor::Renderer& renderer, const EditorContext& ctx);  // só com a janela visível
    // A cena pela câmera principal, com o HUD, num framebuffer qualquer, na hora (captura do MCP, mesmo sem a
    // janela Jogo aberta). false se a cena não tiver câmera ativa.
    bool RenderTo(motor::Renderer& renderer, const EditorContext& ctx, const motor::Framebuffer& target);
    glm::vec2 Size() const { return m_visible ? m_size : glm::vec2(0.0f); }  // na tela, se visível
    void Draw(PlayController& play);

    bool IsFocused() const { return m_focused; }
    void RequestFocus() { m_focusRequest = true; }
    const motor::Framebuffer* Target() const { return m_visible ? m_framebuffer.get() : nullptr; }

private:
    std::unique_ptr<motor::Framebuffer> m_framebuffer;
    std::unique_ptr<motor::UiRenderer> m_ui;
    glm::vec2 m_size{1.0f};
    bool m_visible = false;        // a janela foi desenhada (e não está numa aba escondida) no último quadro
    bool m_focused = false;
    bool m_focusRequest = false;
    bool m_missingCamera = false;
    bool m_wasSideBySide = false;  // para dividir o Viewport só quando o modo é ligado
    bool m_dockPending = false;
};

} // namespace editor
