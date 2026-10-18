#pragma once

#include "EditorContext.h"

#include <motor/ui/UiLayout.h>

#include <glm/vec2.hpp>
#include <imgui.h>

#include <vector>

namespace editor {

// UI do jogo no editor: criar elementos (menu Criar > Interface) e editá-los no viewport (contornos,
// selecionar com o clique e arrastar para mudar a posição). Os campos ficam no Inspector como os de
// qualquer componente; o desfazer vem do Track (um arraste inteiro = um passo).
enum class UiElementKind { Text, Image, Button, Panel };

motor::Entity& CreateUiElement(EditorContext& ctx, UiElementKind kind, EntityId parent = kNullEntity);
void DrawCreateUiMenuItems(EditorContext& ctx, EntityId parent);  // itens do submenu "Interface (UI)"

class UiViewportTool {
public:
    // Contorno fino de todos os elementos e o selecionado em destaque. `layout`: em pixels do viewport.
    void Draw(const EditorContext& ctx, ImDrawList* dl, const ImVec2& origin, const std::vector<motor::UiNode>& layout) const;
    // Clique esquerdo em `mouse` (pixels do viewport): seleciona o elemento de cima e começa a arrastá-lo.
    // false se não havia UI ali (o viewport segue com a seleção 3D).
    bool OnClick(EditorContext& ctx, const glm::vec2& mouse, const std::vector<motor::UiNode>& layout);
    // A cada frame: move o elemento arrastado enquanto o botão esquerdo está segurado.
    void Update(EditorContext& ctx, const glm::vec2& viewportSize);
    bool IsDragging() const { return m_dragging != kNullEntity; }

private:
    EntityId m_dragging = kNullEntity;
    glm::vec2 m_remainder{0.0f};  // fração de pixel ainda não aplicada (a posição anda em pixels inteiros)
};

} // namespace editor
