#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace motor {

struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // identidade (w, x, y, z)
    glm::vec3 scale{1.0f};

    glm::mat4 Matrix() const;  // T * R * S

    void Rotate(const glm::vec3& axis, float radians);
    void SetEulerDegrees(const glm::vec3& degrees);
    // Ângulos de Euler (graus, cada um em (-180, 180]) que dão esta rotação, na leitura que menos gira em X e
    // Z: SetEulerDegrees({0, 180, 0}) volta [0, 180, 0], não [180, 0, 180] (o mesmo giro, escrito de outro jeito).
    glm::vec3 EulerDegrees() const;
};

} // namespace motor
