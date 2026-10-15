#pragma once

namespace motor {

class Camera;
class Window;

// Controle estilo editor:
//   botão direito (segurar) = olhar com o mouse
//   WASD = mover, Q/E = descer/subir, Shift = rápido, scroll = ajusta velocidade
class FlyCameraController {
public:
    explicit FlyCameraController(Camera& camera) : m_camera(camera) {}

    void Update(float dt, Window& window);

    float moveSpeed = 6.0f;
    float boostMultiplier = 4.0f;
    float mouseSensitivity = 0.12f;  // graus por pixel

private:
    Camera& m_camera;
};

} // namespace motor
