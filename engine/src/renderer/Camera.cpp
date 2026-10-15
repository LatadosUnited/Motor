#include "motor/renderer/Camera.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>

namespace motor {

namespace {
constexpr glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
}

glm::vec3 Camera::Forward() const {
    const float y = glm::radians(yaw);
    const float p = glm::radians(pitch);
    return glm::normalize(glm::vec3{std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p)});
}

glm::vec3 Camera::Right() const {
    return glm::normalize(glm::cross(Forward(), kWorldUp));
}

glm::vec3 Camera::Up() const {
    return glm::cross(Right(), Forward());
}

glm::mat4 Camera::View() const {
    return glm::lookAt(position, position + Forward(), kWorldUp);
}

glm::mat4 Camera::Projection() const {
    return glm::perspective(glm::radians(fovY), aspect, nearPlane, farPlane);
}

void Camera::LookAt(const glm::vec3& target) {
    const glm::vec3 d = glm::normalize(target - position);
    pitch = glm::degrees(std::asin(glm::clamp(d.y, -1.0f, 1.0f)));
    yaw = glm::degrees(std::atan2(d.z, d.x));
}

} // namespace motor
