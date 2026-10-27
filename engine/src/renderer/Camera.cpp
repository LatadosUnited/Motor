#include "motor/renderer/Camera.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>

namespace motor {

namespace {
glm::vec3 BaseRight(float yawDegrees) {
    const float y = glm::radians(yawDegrees);
    return {-std::sin(y), 0.0f, std::cos(y)};
}
} // namespace

glm::vec3 Camera::Forward() const {
    const float y = glm::radians(yaw);
    const float p = glm::radians(pitch);
    return glm::normalize(glm::vec3{std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p)});
}

glm::vec3 Camera::Right() const {
    const glm::vec3 baseRight = BaseRight(yaw);
    if (roll == 0.0f) return baseRight;
    const glm::vec3 baseUp = glm::normalize(glm::cross(baseRight, Forward()));
    const float r = glm::radians(roll);
    return glm::normalize(std::cos(r) * baseRight + std::sin(r) * baseUp);
}

glm::vec3 Camera::Up() const {
    const glm::vec3 forward = Forward();
    const glm::vec3 baseRight = BaseRight(yaw);
    const glm::vec3 baseUp = glm::normalize(glm::cross(baseRight, forward));
    if (roll == 0.0f) return baseUp;
    const float r = glm::radians(roll);
    return glm::normalize(-std::sin(r) * baseRight + std::cos(r) * baseUp);
}

glm::mat4 Camera::View() const {
    // Pela direção, não por lookAt(position, position + Forward()): longe da origem a soma em float perde a direção
    // (a 10 km, ~1 milésimo de radiano).
    return glm::translate(ViewRotation(), -position);
}

glm::mat4 Camera::ViewRotation() const {
    return glm::lookAt(glm::vec3(0.0f), Forward(), Up());
}

glm::mat4 Camera::Projection() const {
    return glm::perspective(glm::radians(fovY), aspect, nearPlane, farPlane);
}

void Camera::LookAt(const glm::vec3& target) {
    const glm::vec3 d = glm::normalize(target - position);
    pitch = glm::degrees(std::asin(glm::clamp(d.y, -1.0f, 1.0f)));
    yaw = glm::degrees(std::atan2(d.z, d.x));
    roll = 0.0f;
}

} // namespace motor
