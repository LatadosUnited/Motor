#pragma once

// Layout dos documentos de UI (interno da camada ui): um subconjunto do flex do CSS, feito aqui. Ver o comentário de
// motor/ui/UiDocument.h para o que cada campo faz.

#include "motor/ui/UiLayout.h"

#include <vector>

namespace motor {

struct UiElement;

// Põe os elementos de um documento dentro de `box` (a raiz é uma coluna que estica os filhos) e acrescenta os nós em
// `out`, na ordem de desenho. Escreve no elemento o scrollMax (e prende o scroll nele).
void LayoutUiDocument(EntityId owner, std::vector<UiElement>& elements, const UiBox& box, float scale, int depth,
                      std::vector<UiNode>& out);

} // namespace motor
