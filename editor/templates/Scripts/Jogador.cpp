// Jogador com física: anda com WASD (Shift corre) e pula com Espaço, sem atravessar o chão nem as paredes.
// Precisa do componente "Controlador de personagem" na mesma entidade; o chão e as paredes precisam de Collider.
// W = -Z ("frente" no mundo). Script de exemplo do Motor.
// Guia passo a passo: docs/scripts.md na pasta do Motor (seção "Física" e receita "Jogador").

#include <motor/physics/PhysicsSystem.h>
#include <motor/scene/ScriptModule.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

struct Jogador : motor::Script {
    float velocidade = 4.0f;  // metros por segundo
    float corrida = 2.0f;     // multiplicador com Shift
    float pulo = 5.0f;        // velocidade para cima ao pular (m/s): ~1.3 m de altura
    bool virar = true;        // vira a frente (-Z) para a direção do movimento

    bool querPular = false;  // não registrado: estado interno do Play

    // A tecla é lida a cada frame (OnUpdate) e o pulo vai no passo da física (OnFixedUpdate): assim nenhum
    // aperto de Espaço se perde entre dois passos.
    void OnUpdate(float) override {
        if (WasKeyPressed(motor::Key::Space)) querPular = true;
    }

    void OnFixedUpdate(float) override {
        // A física é um sistema do jogo: existe só no Play (fora dele, nullptr).
        auto* fisica = GetSystem<motor::PhysicsSystem>();
        if (!fisica) return;

        using motor::Key;
        glm::vec3 dir{0.0f};
        if (IsKeyDown(Key::W)) dir.z -= 1.0f;
        if (IsKeyDown(Key::S)) dir.z += 1.0f;
        if (IsKeyDown(Key::A)) dir.x -= 1.0f;
        if (IsKeyDown(Key::D)) dir.x += 1.0f;
        const bool moving = glm::length(dir) > 1e-6f;
        if (moving) dir = glm::normalize(dir);  // na diagonal não anda mais rápido

        const bool running = IsKeyDown(Key::LeftShift) || IsKeyDown(Key::RightShift);
        // MoveCharacter vale até a próxima chamada: chamar a cada passo (com zero, para).
        fisica->MoveCharacter(GetId(), dir * velocidade * (running ? corrida : 1.0f));
        if (querPular) {
            fisica->Jump(GetId(), pulo);  // só pula se estiver no chão
            querPular = false;
        }

        // Frente do objeto é -Z: o ângulo em Y que leva -Z até dir.
        if (virar && moving) {
            GetTransform().rotation = glm::angleAxis(std::atan2(-dir.x, -dir.z), glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }
};

MOTOR_SCRIPT(Jogador) {
    registry.Register<Jogador>("Jogador", "Jogador (física)")
        .Field("velocidade", "Velocidade", &Jogador::velocidade,
               {.speed = 0.1f, .min = 0.0f, .max = 100.0f, .format = "%.1f m/s"})
        .Field("corrida", "Corrida", &Jogador::corrida,
               {.speed = 0.05f, .min = 1.0f, .max = 10.0f, .format = "x%.1f", .tooltip = "Multiplicador com Shift"})
        .Field("pulo", "Pulo", &Jogador::pulo,
               {.speed = 0.05f, .min = 0.0f, .max = 50.0f, .format = "%.1f m/s", .tooltip = "Velocidade para cima ao pular"})
        .Field("virar", "Virar", &Jogador::virar, {.tooltip = "Vira o objeto para a direção do movimento"});
}
