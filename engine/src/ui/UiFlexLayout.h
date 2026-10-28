#pragma once

// Layout dos documentos de UI (interno da camada ui): um subconjunto do flex do CSS, feito aqui. Ver o comentário de
// motor/ui/UiDocument.h para o que cada campo faz.

#include "motor/ui/UiLayout.h"

#include <vector>

namespace motor {

struct UiDocumentData;
struct UiElement;

// Lado do quadrado da caixa de seleção (Toggle), em pixels da tela: um pouco maior que a letra.
float UiToggleBoxSize(const UiElement& element, float scale);
// Slider: o puxador tem a altura da caixa de conteúdo e esta largura; o centro dele anda de x0 a x1 (pixels da tela).
float UiSliderThumbWidth(const UiBox& content);
void UiSliderTrack(const UiBox& content, float& x0, float& x1);

// Põe os elementos de um documento dentro de `box` (a raiz é uma coluna que estica os filhos) e acrescenta os nós em
// `out`, na ordem de desenho. Escreve no elemento o scrollMax (e prende o scroll nele). Nada mudou desde a última
// vez (as caixas, o texto medido, o tamanho da tela): os nós vêm do cache do documento, sem refazer o layout.
void LayoutUiDocument(EntityId owner, UiDocumentData& document, const UiBox& box, float scale, int depth,
                      std::vector<UiNode>& out);

} // namespace motor
