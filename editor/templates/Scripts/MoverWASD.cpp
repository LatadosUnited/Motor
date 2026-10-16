// Move a entidade no plano XZ com WASD (Shift corre). W = -Z ("frente" no mundo). Script de exemplo do Motor.

#include <motor/scene/ScriptModule.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

struct MoverWasd : motor::Script {
    float velocidade = 4.0f;  // metros por segundo
    float corrida = 2.0f;     // multiplicador com Shift
    bool virar = true;        // vira a frente (-Z) para a direção do movimento

    void OnUpdate(float dt) override {
        using motor::Key;
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
};

MOTOR_SCRIPT(MoverWasd) {
    registry.Register<MoverWasd>("MoverWASD", "Mover com WASD")
        .Field("velocidade", "Velocidade", &MoverWasd::velocidade,
               {.speed = 0.1f, .min = 0.0f, .max = 100.0f, .format = "%.1f m/s"})
        .Field("corrida", "Corrida", &MoverWasd::corrida,
               {.speed = 0.05f, .min = 1.0f, .max = 10.0f, .format = "x%.1f", .tooltip = "Multiplicador com Shift"})
        .Field("virar", "Virar", &MoverWasd::virar, {.tooltip = "Vira o objeto para a direção do movimento"});
}
