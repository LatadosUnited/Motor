// Guia de scripts (docs/scripts.md), "Física": uma bola empurrada por forças. As setas empurram (força
// contínua), Espaço dá um impulso para cima se houver chão logo abaixo (raycast) e as pancadas fortes saem
// no log (OnCollisionEnter).
// Na entidade: Mesh Renderer (Esfera), Collider de forma Esfera, Rigidbody e este script.

#include <motor/physics/PhysicsSystem.h>
#include <motor/scene/ScriptModule.h>

#include <glm/geometric.hpp>

struct BolaFisica : motor::Script {
    float forca = 20.0f;        // newtons, enquanto a seta estiver apertada
    float impulso = 5.0f;       // kg·m/s para cima
    float pancadaForte = 3.0f;  // m/s: abaixo disso não escreve no log

    bool querPular = false;

    void OnUpdate(float /*dt*/) override {
        // Teclas "apertadas agora" são lidas no OnUpdate (todo frame) e usadas no passo da física.
        if (WasKeyPressed(motor::Key::Space)) querPular = true;
    }

    void OnFixedUpdate(float /*dt*/) override {
        auto* fisica = GetSystem<motor::PhysicsSystem>();
        if (!fisica) return;

        using motor::Key;
        glm::vec3 empurrao{0.0f};
        if (IsKeyDown(Key::Up)) empurrao.z -= forca;
        if (IsKeyDown(Key::Down)) empurrao.z += forca;
        if (IsKeyDown(Key::Left)) empurrao.x -= forca;
        if (IsKeyDown(Key::Right)) empurrao.x += forca;
        fisica->AddForce(GetId(), empurrao);  // vale só para o próximo passo: chame de novo a cada passo

        if (querPular) {
            querPular = false;
            const glm::vec3 centro = GetTransform().position;  // a bola está na raiz da hierarquia
            const float raio = 0.5f * GetTransform().scale.y;
            if (fisica->Raycast(centro, glm::vec3(0.0f, -1.0f, 0.0f), raio + 0.1f, GetId())) {
                fisica->AddImpulse(GetId(), glm::vec3(0.0f, impulso, 0.0f));
            }
        }
    }

    void OnCollisionEnter(const motor::Collision& c) override {
        if (c.relativeSpeed >= pancadaForte) {
            Log("pancada em {} a {:.1f} m/s (velocidade agora: {:.1f} m/s)", c.other, c.relativeSpeed,
                glm::length(GetSystem<motor::PhysicsSystem>()->GetVelocity(GetId())));
        }
    }
};

MOTOR_SCRIPT(BolaFisica) {
    registry.Register<BolaFisica>("BolaFisica", "Bola (física)")
        .Field("forca", "Força", &BolaFisica::forca, {.speed = 0.5f, .min = 0.0f, .max = 10000.0f, .format = "%.1f N"})
        .Field("impulso", "Impulso", &BolaFisica::impulso, {.speed = 0.1f, .min = 0.0f, .max = 1000.0f})
        .Field("pancadaForte", "Pancada forte", &BolaFisica::pancadaForte, {.speed = 0.1f, .format = "%.1f m/s"});
}
