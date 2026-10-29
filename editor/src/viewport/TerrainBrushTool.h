#pragma once

#include "EditorContext.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>

#include <functional>
#include <optional>
#include <utility>

namespace editor {

// Pincel do terreno no Viewport (Fase 28): com o pincel ligado no Inspector (EditorContext::terrainBrush) e um
// terreno selecionado, o círculo segue o mouse sobre a superfície e o botão esquerdo pinta em vez de selecionar.
// Cada pincelada (do apertar ao soltar) vira um passo de desfazer. Esc desliga o pincel.
class TerrainBrushTool {
public:
    using RayFn = std::function<std::pair<glm::vec3, glm::vec3>(const glm::vec2& uv)>;  // origem e direção no mundo
    using ProjectFn = std::function<std::optional<ImVec2>(const glm::vec3& world)>;     // pixels do viewport

    // A cada quadro, fora da visão do jogo. `hovered`: mouse sobre a imagem da cena (sem gizmo nem botões por cima);
    // `mouseUv`: posição do mouse no viewport, (0, 0) = canto superior esquerdo. Devolve true se o pincel está em
    // uso (o clique esquerdo é dele, não da seleção nem do gizmo).
    bool Update(EditorContext& ctx, bool hovered, bool focused, const glm::vec2& mouseUv, const RayFn& ray,
                const ProjectFn& project, ImDrawList* dl, const ImVec2& origin);
    // Termina a pincelada em andamento (Play, troca de cena).
    void Cancel(EditorContext& ctx);

    // Terreno selecionado com o pincel ligado: o Viewport esconde o gizmo.
    static bool Enabled(const EditorContext& ctx);

private:
    std::optional<motor::TerrainStroke> m_stroke;
    EntityId m_entity = kNullEntity;  // dono da pincelada em andamento
};

} // namespace editor
