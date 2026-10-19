#pragma once

#include "motor/project/CollisionLayers.h"
#include "motor/scene/Component.h"

namespace motor {

// Campo de camada de colisão (Collider, CharacterController). Na cena é o índice (0 a 15); o editor mostra os
// nomes das camadas do projeto.
template <> struct FieldTraits<CollisionLayer> {
    static CustomFieldType Describe();
};

} // namespace motor
