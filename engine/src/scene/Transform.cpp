#include "motor/scene/Transform.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

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

namespace {
// Ângulo em (-180, 180], arredondado a milésimos de grau (179.99998 vira 180, -0 vira 0).
float WrapDegrees(float degrees) {
    float d = std::fmod(degrees, 360.0f);
    if (d <= -180.0f) d += 360.0f;
    if (d > 180.0f) d -= 360.0f;
    d = std::round(d * 1000.0f) / 1000.0f;
    return d == -180.0f ? 180.0f : d + 0.0f;
}
} // namespace

glm::vec3 Transform::EulerDegrees() const {
    // Toda rotação tem duas leituras em Euler: (x, y, z) e (x + 180, 180 - y, z + 180). A conversão do GLM
    // prende y em [-90, 90] e por isso devolve [180, 0, 180] para quem pediu [0, 180, 0]; ficamos com a que
    // gira menos em X e Z, que é a que as pessoas escrevem.
    const glm::vec3 a = glm::degrees(glm::eulerAngles(rotation));
    const glm::vec3 first{WrapDegrees(a.x), WrapDegrees(a.y), WrapDegrees(a.z)};
    const glm::vec3 second{WrapDegrees(a.x + 180.0f), WrapDegrees(180.0f - a.y), WrapDegrees(a.z + 180.0f)};
    const float costFirst = std::abs(first.x) + std::abs(first.z);
    const float costSecond = std::abs(second.x) + std::abs(second.z);
    return costSecond + 1e-3f < costFirst ? second : first;
}

} // namespace motor
