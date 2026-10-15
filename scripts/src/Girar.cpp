#include "scripts/Scripts.h"

#include <motor/scene/Transform.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

namespace scripts {

void Girar::OnUpdate(float dt) {
    if (glm::length(eixo) < 1e-6f) return;
    const glm::quat delta = glm::angleAxis(glm::radians(velocidade * dt), glm::normalize(eixo));
    motor::Transform& t = GetTransform();
    t.rotation = glm::normalize(local ? t.rotation * delta : delta * t.rotation);
}

} // namespace scripts
