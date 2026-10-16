#pragma once

// Contorno dos colliders e personagens em linhas, no espaço do mundo, para o editor desenhar por cima da cena.
// As medidas seguem as mesmas regras do PhysicsSystem (escala do mundo, centro, cápsula de pé no Y local).

#include "motor/scene/CharacterControllerComponent.h"
#include "motor/scene/ColliderComponent.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <vector>

namespace motor {

struct GizmoLine {
    glm::vec3 a{0.0f};
    glm::vec3 b{0.0f};
};

// `meshMin`/`meshMax`: limites locais da malha do MeshRenderer (forma Malha, que aparece como essa caixa).
// Sem malha (nullptr), a forma Malha aparece como uma caixa 1 x 1 x 1, como no PhysicsSystem.
void AppendColliderLines(const ColliderComponent& collider, const glm::mat4& world, std::vector<GizmoLine>& out,
                         const glm::vec3* meshMin = nullptr, const glm::vec3* meshMax = nullptr);

// Cápsula do personagem, sempre de pé, centrada na posição da entidade.
void AppendCharacterLines(const CharacterControllerComponent& controller, const glm::mat4& world,
                          std::vector<GizmoLine>& out);

} // namespace motor
