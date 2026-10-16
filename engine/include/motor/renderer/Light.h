#pragma once

#include <glm/vec3.hpp>

#include <cstddef>

namespace motor {

// A ordem é a do seletor no editor e o número aceito na cena: só acrescente no fim.
enum class LightType { Directional = 0, Point = 1, Spot = 2 };

// Uma luz já no espaço do mundo, do jeito que o shader recebe. A cena monta a lista a partir dos componentes
// Luz (Scene::CollectLights); quem usa só o renderer (sandbox) monta à mão.
struct Light {
    LightType type = LightType::Directional;
    glm::vec3 position{0.0f};                  // pontual e spot
    glm::vec3 direction{-0.4f, -1.0f, -0.3f};  // direcional e spot: para onde a luz aponta
    glm::vec3 color{1.0f, 0.97f, 0.9f};        // usada direto no espaço linear do shader
    float intensity = 1.0f;
    float range = 10.0f;       // pontual e spot: distância em que a luz chega a zero
    float innerAngle = 25.0f;  // spot: meio ângulo (graus) com luz total
    float outerAngle = 35.0f;  // spot: meio ângulo (graus) onde a luz acaba
};

// Limite por passada (tamanho do bloco de luzes no shader). As que passarem disso são ignoradas.
inline constexpr size_t kMaxLights = 32;

} // namespace motor
