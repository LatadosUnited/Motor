// Move a entidade no plano XZ com WASD, as setas ou o analógico do controle (ações Horizontal e Vertical; Correr
// corre). W = -Z ("frente" no mundo). Script de exemplo do Motor.
// Mexe direto no Transform, sem física: atravessa paredes. Para andar com colisão, veja o modelo Jogador.
// Guia passo a passo: docs/scripts.md na pasta do Motor (seção "Entrada e tempo").

#include <motor/scene/ScriptModule.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

struct MoverWasd : motor::Script {
    float velocidade = 4.0f;  // metros por segundo
    float corrida = 2.0f;     // multiplicador ao correr
    bool virar = true;        // vira a frente (-Z) para a direção do movimento

    void OnUpdate(float dt) override {
        // GetAxis: a ação do projeto (Arquivo > Controles), de -1 a 1; zero quando o jogo não tem o foco (ex.:
        // digitando no editor). Vertical para cima (W, analógico para a frente) = -Z.
        glm::vec3 dir{GetAxis("Horizontal"), 0.0f, -GetAxis("Vertical")};
        if (glm::length(dir) < 1e-3f) return;
        if (glm::length(dir) > 1.0f) dir = glm::normalize(dir);  // na diagonal do teclado não anda mais rápido

        const bool running = IsActionDown("Correr");
        motor::Transform& t = GetTransform();
        // Velocidade (m/s) vezes dt (s) = metros andados neste frame.
        t.position += dir * velocidade * (running ? corrida : 1.0f) * dt;

        // Frente do objeto é -Z: o ângulo em Y que leva -Z até dir.
        if (virar) t.rotation = glm::angleAxis(std::atan2(-dir.x, -dir.z), glm::vec3(0.0f, 1.0f, 0.0f));
    }
};

MOTOR_SCRIPT(MoverWasd) {
    registry.Register<MoverWasd>("MoverWASD", "Mover (WASD ou controle)")
        .Field("velocidade", "Velocidade", &MoverWasd::velocidade,
               {.speed = 0.1f, .min = 0.0f, .max = 100.0f, .format = "%.1f m/s"})
        .Field("corrida", "Corrida", &MoverWasd::corrida,
               {.speed = 0.05f, .min = 1.0f, .max = 10.0f, .format = "x%.1f", .tooltip = "Multiplicador ao correr (ação Correr)"})
        .Field("virar", "Virar", &MoverWasd::virar, {.tooltip = "Vira o objeto para a direção do movimento"});
}
