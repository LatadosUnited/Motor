#pragma once

#include "motor/scene/CollisionLayerField.h"
#include "motor/scene/Component.h"

#include <glm/vec3.hpp>

namespace motor {

// Forma do collider. Mesh usa a malha do MeshRenderer da mesma entidade.
enum class ColliderShape { Box, Sphere, Capsule, Mesh, Cylinder };

// Forma de colisão da entidade (física, Fase 5). Sem Rigidbody o corpo é estático (chão, paredes); com
// Rigidbody ele se move. As medidas são locais: a escala da entidade (no mundo) multiplica todas.
// A cápsula e o cilindro ficam de pé no eixo Y local.
struct ColliderComponent {
    ColliderShape shape = ColliderShape::Box;
    glm::vec3 size{1.0f, 1.0f, 1.0f};  // caixa: tamanho total (o cubo embutido tem 1 x 1 x 1)
    float radius = 0.5f;               // esfera, cápsula e cilindro
    float height = 2.0f;               // cápsula (altura total, com as pontas) e cilindro
    glm::vec3 center{0.0f};            // deslocamento da forma em relação à origem da entidade
    bool isTrigger = false;            // só detecta (OnTriggerEnter/Exit), não empurra
    float friction = 0.5f;
    float restitution = 0.0f;  // 0 = não quica, 1 = quica sem perder energia
    CollisionLayer layer;      // camada de colisão do projeto (quem se choca com quem: CollisionLayers)

    bool operator==(const ColliderComponent&) const = default;
};

// Na cena a forma é gravada como texto ("Box", "Sphere", "Capsule", "Mesh"); números também são aceitos.
template <> struct FieldTraits<ColliderShape> {
    static CustomFieldType Describe();
};

const char* ColliderShapeName(ColliderShape shape);   // identificador gravado na cena
const char* ColliderShapeLabel(ColliderShape shape);  // nome no editor ("Caixa")

} // namespace motor
