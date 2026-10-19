#pragma once

#include "motor/scene/PrefabInstance.h"

namespace motor {

// Prefab dentro de prefab. Num .mprefab A, as entidades de uma instância do prefab B guardam aqui o vínculo com
// B (qual entidade de B, e o que A mudou nela); o PrefabInstance delas continua dizendo qual entidade de A elas
// são. Quando B muda, o conteúdo de A segue B (menos o que A mudou) e as instâncias de A na cena seguem A
// (Prefabs::UpdateNested). Na cena, as cópias desse componente só informam de onde a entidade veio; desfazer o
// vínculo com A (Prefabs::Unpack) as transforma em instâncias de B.
//
// Criado e mantido pelo editor ao criar ou aplicar um prefab que contém instâncias de outros (não aparece em
// "Adicionar componente" nem no Inspector).
struct PrefabNested {
    PrefabLink link;
};

} // namespace motor
