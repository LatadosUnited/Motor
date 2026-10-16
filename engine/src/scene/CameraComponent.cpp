#include "motor/scene/CameraComponent.h"

#include "motor/scene/Component.h"

namespace motor {

MOTOR_COMPONENT_MODULE(Camera) {
    registry.Register<CameraComponent>("Camera", "Câmera")
        .Field("fov", "Campo de visão", &CameraComponent::fov, {.slider = true, .min = 10.0f, .max = 120.0f, .format = "%.0f°"})
        .Field("near", "Perto", &CameraComponent::nearPlane,
               {.speed = 0.01f, .min = 0.01f, .max = 10.0f, .format = "%.2f m", .tooltip = "Plano de corte próximo"})
        .Field("far", "Longe", &CameraComponent::farPlane,
               {.speed = 1.0f, .min = 1.0f, .max = 10000.0f, .format = "%.0f m", .tooltip = "Plano de corte distante"})
        .Field("main", "Principal", &CameraComponent::main,
               {.tooltip = "No Play, a cena é vista pela primeira câmera ativa marcada como principal"});
}

} // namespace motor
