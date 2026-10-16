#include "motor/scene/RigidbodyComponent.h"

namespace motor {

MOTOR_COMPONENT_MODULE(Rigidbody) {
    registry.Register<RigidbodyComponent>("Rigidbody", "Rigidbody")
        .Field("mass", "Massa", &RigidbodyComponent::mass,
               {.speed = 0.05f, .min = 0.001f, .max = 100000.0f, .format = "%.2f kg"})
        .Field("useGravity", "Gravidade", &RigidbodyComponent::useGravity)
        .Field("isKinematic", "Cinemático", &RigidbodyComponent::isKinematic,
               {.tooltip = "Não cai nem é empurrado: segue o Transform e empurra os outros corpos"})
        .Field("linearDamping", "Freio do movimento", &RigidbodyComponent::linearDamping,
               {.slider = true, .min = 0.0f, .max = 1.0f, .format = "%.2f"})
        .Field("angularDamping", "Freio da rotação", &RigidbodyComponent::angularDamping,
               {.slider = true, .min = 0.0f, .max = 1.0f, .format = "%.2f"});
}

} // namespace motor
