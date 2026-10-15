#include "scripts/Scripts.h"

#include <motor/scene/Transform.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace scripts {

using motor::Key;

void MoverWasd::OnUpdate(float dt) {
    glm::vec3 dir{0.0f};
    if (IsKeyDown(Key::W)) dir.z -= 1.0f;
    if (IsKeyDown(Key::S)) dir.z += 1.0f;
    if (IsKeyDown(Key::A)) dir.x -= 1.0f;
    if (IsKeyDown(Key::D)) dir.x += 1.0f;
    if (glm::length(dir) < 1e-6f) return;
    dir = glm::normalize(dir);  // na diagonal não anda mais rápido

    const bool running = IsKeyDown(Key::LeftShift) || IsKeyDown(Key::RightShift);
    motor::Transform& t = GetTransform();
    t.position += dir * velocidade * (running ? corrida : 1.0f) * dt;

    // Frente do objeto é -Z: o ângulo em Y que leva -Z até dir.
    if (virar) t.rotation = glm::angleAxis(std::atan2(-dir.x, -dir.z), glm::vec3(0.0f, 1.0f, 0.0f));
}

} // namespace scripts
