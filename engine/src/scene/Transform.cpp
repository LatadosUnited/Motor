#include "motor/scene/Transform.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace motor {

glm::mat4 Transform::Matrix() const {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
    m *= glm::mat4_cast(rotation);
    return glm::scale(m, scale);
}

void Transform::Rotate(const glm::vec3& axis, float radians) {
    rotation = glm::normalize(glm::angleAxis(radians, glm::normalize(axis)) * rotation);
}

void Transform::SetEulerDegrees(const glm::vec3& degrees) {
    rotation = glm::quat(glm::radians(degrees));
}

} // namespace motor
