#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace motor {

// Câmera em perspectiva orientada por yaw/pitch (graus). Mundo: Y para cima, -Z para frente.
class Camera {
public:
    glm::vec3 position{0.0f, 2.0f, 6.0f};
    float yaw = -90.0f;   // -90 => olhando para -Z
    float pitch = -10.0f;

    float fovY = 60.0f;   // graus
    float aspect = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 500.0f;

    glm::vec3 Forward() const;
    glm::vec3 Right() const;
    glm::vec3 Up() const;

    glm::mat4 View() const;
    glm::mat4 Projection() const;

    void LookAt(const glm::vec3& target);
};

} // namespace motor
