#pragma once

#include "EditorContext.h"
#include "viewport/FrameStatsPanel.h"
#include "viewport/NavMeshOverlay.h"
#include "viewport/ViewportPrefs.h"
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
    // O mesmo desenho num framebuffer qualquer, na hora (captura do MCP: vale com o editor sem foco, minimizado
    // ou com o Viewport numa aba escondida). false se, pela câmera do jogo, a cena não tiver câmera.
    bool RenderTo(motor::Renderer& renderer, const EditorContext& ctx, bool gameView, const motor::Framebuffer& target);
    // Tamanho do Viewport na tela (o da última vez que ele foi desenhado).
    glm::vec2 Size() const { return m_size; }
    void Draw(EditorContext& ctx, PlayController& play, const motor::RenderStats& stats, float fps);

    void FocusSelected(const EditorContext& ctx);
    // Câmera livre do editor (ferramenta view_camera do MCP). Sem `target`, mantém para onde olha.
    const motor::Camera& EditorCamera() const { return m_camera; }
    void SetEditorCamera(const glm::vec3& position, const std::optional<glm::vec3>& target) {
        m_camera.position = position;
        if (target) m_camera.LookAt(*target);
    }
    void HandleGizmoShortcuts();  // Q/W/E/R/X, sem Ctrl

    // Imagem do último quadro desenhado (cena e HUD, sem os controles do ImGui por cima).
    const motor::Framebuffer* Target() const { return m_framebuffer.get(); }

    bool IsLooking() const { return m_looking; }
    bool IsFocused() const { return m_focused; }
    void StopLooking() { m_looking = false; }
    void RequestFocus() { m_focusRequest = true; }

    // Contornos de collider, encaixe e grade (lembrados no editor.json pelo EditorApp).
    const ViewportPrefs& Prefs() const { return m_prefs; }
    void SetPrefs(const ViewportPrefs& prefs) {
        m_prefs = prefs;
        m_prefs.Clamp();
    }

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
    // Contorno dos colliders e personagens (física), em linhas por cima da imagem, conforme m_prefs.colliders.
    void DrawColliders(const EditorContext& ctx, ImDrawList* dl, const ImVec2& origin);
    // Ponto do mundo em pixels do viewport (relativo ao canto); nullopt se estiver atrás da câmera.
    std::optional<ImVec2> Project(const glm::vec3& world) const;
    void DrawGizmo(EditorContext& ctx, const ImVec2& origin);
    void DrawStatsOverlay(ImDrawList* dl, const ImVec2& origin, float playLeft, bool gameView,
                          const motor::RenderStats& stats, float fps);
    void DrawPlayIndicator(ImDrawList* dl, const ImVec2& origin, const PlayController& play, bool gameView) const;
    void DrawToolbar(const ImVec2& position);
    void DrawSnapPopup();  // passos do encaixe e grade (botão ao lado do Snap)
    void SubmitGrid(motor::Renderer& renderer) const;
    void PickAt(EditorContext& ctx, const glm::vec2& uv);  // uv no viewport, (0,0) = canto superior esquerdo
    // Botão esquerdo pressionado na cena: ao soltar, clique (PickAt) ou retângulo de seleção, se arrastou.
    void UpdateBoxSelection(EditorContext& ctx, const ImVec2& origin, bool gameView);
    std::pair<glm::vec3, glm::vec3> Ray(const glm::vec2& uv) const;  // origem, direção (não normalizada)
    void HandleDrop(EditorContext& ctx, const ImVec2& origin);

    motor::Camera m_camera;
    motor::FlyCameraController m_cameraController{m_camera};
    std::unique_ptr<motor::Framebuffer> m_framebuffer;
    std::unique_ptr<motor::UiRenderer> m_ui;
    UiViewportTool m_uiTool;
    FrameStatsPanel m_statsPanel;  // contadores por quadro (Fase 22), com m_prefs.stats
    NavMeshOverlay m_navOverlay;   // navmesh e caminhos dos agentes (Fase 34), com m_prefs.navMesh
    bool m_showUi = true;

    enum class GizmoTool { None, Translate, Rotate, Scale };
    GizmoTool m_gizmoTool = GizmoTool::Translate;
    bool m_gizmoLocal = false;  // false = eixos do mundo
    ViewportPrefs m_prefs;
    std::shared_ptr<motor::Mesh> m_gridMesh;       // linhas da grade fina (CreateGridMesh), escalada pela célula
    std::shared_ptr<motor::Mesh> m_gridMajorMesh;  // grade grossa (a cada 10), mais curta em células
    std::shared_ptr<motor::Mesh> m_axisMesh;  // uma linha só, para os eixos X e Z

    std::vector<Icon> m_icons;  // do frame atual
    std::vector<motor::GizmoLine> m_lines;  // reaproveitado entre entidades (sem alocar a cada frame)
    glm::vec2 m_size{1.0f};
    bool m_looking = false;            // botão direito segurado no viewport
    bool m_wasLooking = false;         // no quadro anterior (soltar o cursor só ao parar de voar)
    bool m_focused = false;            // o jogo só recebe teclado/mouse com o viewport em foco
    bool m_focusRequest = false;
    bool m_missingGameCamera = false;  // no Play sem câmera na cena: mostra pela câmera do editor
    bool m_boxPending = false;         // botão esquerdo pressionado na cena, ainda não solto
    ImVec2 m_boxStart;                 // onde foi pressionado (relativo ao canto do viewport)
};

} // namespace editor
