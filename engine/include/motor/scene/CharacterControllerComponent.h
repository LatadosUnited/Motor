#pragma once

#include "motor/scene/CollisionLayerField.h"
#include "motor/scene/Component.h"

namespace motor {

// Personagem controlado por script (jogador): uma cápsula de pé que anda, sobe degraus e rampas até a
// inclinação máxima e não atravessa o chão. Não gira com a física. A origem da entidade é o centro da
// cápsula. O script pede o movimento ao PhysicsSystem (MoveCharacter, Jump); a gravidade é do sistema.
struct CharacterControllerComponent {
    float height = 1.8f;      // altura total, com as pontas
    float radius = 0.3f;
    float maxSlope = 45.0f;   // graus: rampa mais íngreme que ainda dá para subir
    float stepHeight = 0.3f;  // degrau mais alto que sobe sozinho
    float mass = 70.0f;       // kg: quanto empurra caixas
    CollisionLayer layer;     // camada de colisão do projeto
    bool useGravity = true;   // sem gravidade o personagem flutua (escada, nado, voo); mudar não refaz o corpo

    bool operator==(const CharacterControllerComponent&) const = default;
};

} // namespace motor
