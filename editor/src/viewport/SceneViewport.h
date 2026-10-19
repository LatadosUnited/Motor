#pragma once

#include "EditorContext.h"
#include "viewport/UiEditor.h"

#include <motor/Motor.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace editor {

class PlayController;

// Janela "Viewport": câmera livre do editor, imagem da cena, gizmo de transform, seleção por clique e
// arquivos soltos do painel Projeto. No Play mostra a cena pela câmera do jogo (ver PlayController).
// A UI do jogo (camada ui/) é desenhada por cima da cena; fora da visão do jogo, os elementos têm contorno,
// são selecionados com o clique e arrastados (UiViewportTool). Na visão do jogo, o mouse vai para o UiSystem.
class SceneViewport {
public:
    void Init();

    // Câmera livre (botão direito segurado) e captura do mouse.
    void UpdateCamera(float dt, motor::Window& window);
    // Desenha a cena no framebuffer do viewport. `gameView`: pela câmera principal da cena.
    void Render(motor::Renderer& renderer, const EditorContext& ctx, bool gameView);
    void Draw(EditorContext& ctx, PlayController& play, const motor::RenderStats& stats, float fps);

    void FocusSelected(const EditorContext& ctx);
    void HandleGizmoShortcuts();  // Q/W/E/R/X, sem Ctrl

    // Imagem do último quadro desenhado (cena e HUD, sem os controles do ImGui por cima).
    const motor::Framebuffer* Target() const { return m_framebuffer.get(); }

    bool IsLooking() const { return m_looking; }
    bool IsFocused() const { return m_focused; }
    void StopLooking() { m_looking = false; }
    void RequestFocus() { m_focusRequest = true; }

private:
    // Ícones de luzes e câmeras (que não têm malha para clicar): desenhados por cima da imagem e
    // selecionáveis com o clique, antes do raio contra as malhas.
    struct Icon {
        EntityId entity = kNullEntity;
        ImVec2 center;       // em pixels, relativo ao canto superior esquerdo do viewport
        float depth = 0.0f;  // distância até a câmera do editor (o mais perto ganha no clique)
    };
    void CollectIcons(const EditorContext& ctx);
    void DrawIcons(const EditorContext& ctx, ImDrawList* dl, const ImVec2& origin) const;
    void DrawSelectedHelpers(const EditorContext& ctx, ImDrawList* dl, const ImVec2& origin) const;
    // Contorno dos colliders e personagens (física), em linhas por cima da imagem.
    void DrawColliders(const EditorContext& ctx, ImDrawList* dl, const ImVec2& origin);
    // Ponto do mundo em pixels do viewport (relativo ao canto); nullopt se estiver atrás da câmera.
    std::optional<ImVec2> Project(const glm::vec3& world) const;
    void DrawGizmo(EditorContext& ctx, const ImVec2& origin);
    void DrawToolbar(const ImVec2& position);
    void PickAt(EditorContext& ctx, const glm::vec2& uv);  // uv no viewport, (0,0) = canto superior esquerdo
    std::pair<glm::vec3, glm::vec3> Ray(const glm::vec2& uv) const;  // origem, direção (não normalizada)
    void HandleDrop(EditorContext& ctx, const ImVec2& origin);

    motor::Camera m_camera;
    motor::FlyCameraController m_cameraController{m_camera};
    std::unique_ptr<motor::Framebuffer> m_framebuffer;
    std::unique_ptr<motor::UiRenderer> m_ui;
    UiViewportTool m_uiTool;
    bool m_showUi = true;

    enum class GizmoTool { None, Translate, Rotate, Scale };
    GizmoTool m_gizmoTool = GizmoTool::Translate;
    bool m_gizmoLocal = false;  // false = eixos do mundo
    bool m_snap = false;
    float m_snapTranslate = 0.5f;
    float m_snapRotate = 15.0f;  // graus
    float m_snapScale = 0.1f;

    std::vector<Icon> m_icons;  // do frame atual
    std::vector<motor::GizmoLine> m_lines;  // reaproveitado entre entidades (sem alocar a cada frame)
    bool m_showColliders = true;
    glm::vec2 m_size{1.0f};
    bool m_looking = false;            // botão direito segurado no viewport
    bool m_wasLooking = false;         // no quadro anterior (soltar o cursor só ao parar de voar)
    bool m_focused = false;            // o jogo só recebe teclado/mouse com o viewport em foco
    bool m_focusRequest = false;
    bool m_missingGameCamera = false;  // no Play sem câmera na cena: mostra pela câmera do editor
};

} // namespace editor
