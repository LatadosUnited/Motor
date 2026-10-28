#pragma once

// Layout da UI: onde cada elemento fica na tela, a partir das âncoras (UiRect). Só CPU, sem OpenGL.
//
// Unidades: os valores dos componentes estão em pixels de uma tela de referência com kUiReferenceHeight de
// altura. Numa tela de verdade tudo é multiplicado por UiScale (altura da tela / referência): o HUD fica
// proporcional no viewport do editor e em qualquer janela. Os retângulos calculados já estão em pixels reais.

#include "motor/ui/UiComponents.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <vector>

namespace motor {

class Scene;
using EntityId = uint32_t;

inline constexpr float kUiReferenceHeight = 1080.0f;

inline float UiScale(const glm::vec2& screenSize) {
    return screenSize.y > 0.0f ? screenSize.y / kUiReferenceHeight : 1.0f;
}

// Retângulo em pixels da tela (origem no canto superior esquerdo, y para baixo).
struct UiBox {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};

    glm::vec2 Size() const { return max - min; }
    glm::vec2 Center() const { return (min + max) * 0.5f; }
    bool Contains(const glm::vec2& p) const { return p.x >= min.x && p.y >= min.y && p.x < max.x && p.y < max.y; }
    bool operator==(const UiBox&) const = default;
};

// Fração (0 a 1) do pai onde fica a âncora (e o ponto do elemento preso nela).
glm::vec2 UiAnchorPoint(UiAnchor anchor);

// Retângulo de um elemento dentro do pai, com a escala da tela.
UiBox ResolveUiRect(const UiRect& rect, const UiBox& parent, float scale);

struct UiElement;

struct UiNode {
    EntityId entity = 0;  // a entidade de UI, ou a do UiDocument quando é um elemento dele
    UiBox box;
    int depth = 0;  // 0 = elemento de UI sem pai de UI
    UiElement* element = nullptr;  // elemento da árvore viva do documento (UiDocument::Tree), ou nulo
    UiBox content;         // dentro do padding (onde vai o texto); igual a `box` sem padding
    UiBox clip;            // recorte de um pai com overflow Hidden/Scroll, se `clipped`
    bool clipped = false;
};

// Elementos de UI visíveis (entidade ativa na hierarquia e UiRect::visible, também dos pais), na ordem de
// desenho: pai antes dos filhos, irmãos por UiRect::order (empate: ordem da cena). O último é o de cima.
// Uma entidade com UiDocument não gera nó: os elementos do documento entram no lugar dela, ocupando o pai.
std::vector<UiNode> ComputeUiLayout(const Scene& scene, const glm::vec2& screenSize);

enum class UiHitFilter {
    Any,        // qualquer elemento (seleção no editor)
    Blocking,   // o de cima que tem imagem ou botão (texto sozinho não bloqueia o clique)
};

// Elemento sob o ponto (pixels da tela), o de cima primeiro. 0 se nenhum (num documento, a entidade dele).
EntityId UiHitTest(const Scene& scene, const std::vector<UiNode>& layout, const glm::vec2& point,
                   UiHitFilter filter = UiHitFilter::Blocking);
// O mesmo, devolvendo o nó (com o elemento do documento). nullptr se nenhum.
const UiNode* UiHitTestNode(const Scene& scene, const std::vector<UiNode>& layout, const glm::vec2& point,
                            UiHitFilter filter = UiHitFilter::Blocking);

} // namespace motor
