#include "motor/scene/FlyCameraController.h"

#include "motor/core/Input.h"
#include "motor/core/Window.h"
#include "motor/renderer/Camera.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace motor {

void FlyCameraController::Update(float dt, Window& window) {
    const bool looking = Input::IsMouseDown(MouseButton::Right);
    window.SetCursorCaptured(looking);

    if (looking) {
        const glm::vec2 delta = Input::MouseDelta() * mouseSensitivity;
        m_camera.yaw += delta.x;
        m_camera.pitch = glm::clamp(m_camera.pitch - delta.y, -89.0f, 89.0f);

        const float scroll = Input::ScrollDelta();
        if (scroll != 0.0f) moveSpeed = glm::clamp(moveSpeed * (1.0f + 0.1f * scroll), 0.5f, 200.0f);
    }

    glm::vec3 move{0.0f};
    if (Input::IsKeyDown(Key::W)) move += m_camera.Forward();
    if (Input::IsKeyDown(Key::S)) move -= m_camera.Forward();
    if (Input::IsKeyDown(Key::D)) move += m_camera.Right();
    if (Input::IsKeyDown(Key::A)) move -= m_camera.Right();
    if (Input::IsKeyDown(Key::E)) move.y += 1.0f;
    if (Input::IsKeyDown(Key::Q)) move.y -= 1.0f;

    if (glm::dot(move, move) > 0.0f) {
        float speed = moveSpeed;
        if (Input::IsKeyDown(Key::LeftShift)) speed *= boostMultiplier;
        m_camera.position += glm::normalize(move) * speed * dt;
    }

    m_camera.aspect = window.AspectRatio();
}

} // namespace motor
