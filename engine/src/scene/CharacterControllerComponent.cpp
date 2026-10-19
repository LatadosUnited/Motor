#include "motor/scene/CharacterControllerComponent.h"

namespace motor {

MOTOR_COMPONENT_MODULE(CharacterController) {
    registry.Register<CharacterControllerComponent>("CharacterController", "Controlador de personagem")
        .Field("height", "Altura", &CharacterControllerComponent::height,
               {.speed = 0.01f, .min = 0.05f, .max = 100.0f, .format = "%.2f m", .tooltip = "Altura total da cápsula"})
        .Field("radius", "Raio", &CharacterControllerComponent::radius,
               {.speed = 0.01f, .min = 0.01f, .max = 50.0f, .format = "%.2f m"})
        .Field("maxSlope", "Inclinação máxima", &CharacterControllerComponent::maxSlope,
               {.slider = true, .min = 0.0f, .max = 89.0f, .format = "%.0f°",
                .tooltip = "Rampa mais íngreme que o personagem ainda sobe"})
        .Field("stepHeight", "Altura do degrau", &CharacterControllerComponent::stepHeight,
               {.speed = 0.01f, .min = 0.0f, .max = 10.0f, .format = "%.2f m",
                .tooltip = "Degrau mais alto que o personagem sobe sozinho"})
        .Field("mass", "Massa", &CharacterControllerComponent::mass,
               {.speed = 0.5f, .min = 0.1f, .max = 100000.0f, .format = "%.1f kg",
                .tooltip = "Quanto o personagem empurra os corpos que encontra"})
        .Field("useGravity", "Usa gravidade", &CharacterControllerComponent::useGravity,
               {.tooltip = "Desligada, o personagem não cai: para escadas, nado ou voo"})
        .Field("layer", "Camada", &CharacterControllerComponent::layer,
               {.tooltip = "Camada de colisão: quem se choca com quem fica em Projeto > Camadas de colisão"});
}

} // namespace motor
