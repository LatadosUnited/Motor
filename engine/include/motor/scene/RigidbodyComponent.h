#pragma once

#include "motor/scene/Component.h"

namespace motor {

// Corpo que a física move (Fase 5). Precisa de um Collider na mesma entidade para ter forma.
// Cinemático: não cai nem é empurrado; segue o Transform (mexido por script ou animação) e empurra os outros.
struct RigidbodyComponent {
    float mass = 1.0f;  // kg
    bool useGravity = true;
    bool isKinematic = false;
    float linearDamping = 0.05f;   // freio do movimento (resistência do ar)
    float angularDamping = 0.05f;  // freio da rotação

    bool operator==(const RigidbodyComponent&) const = default;
};

} // namespace motor
