// Guia de scripts (docs/scripts.md), "Achar, criar e destruir entidades": um projétil que anda em linha reta
// e se destrói sozinho. Fica num header para o Disparador.cpp poder adicioná-lo às entidades que cria; o
// registro (MOTOR_SCRIPT) fica no Projetil.cpp.
#pragma once

#include <motor/scene/ScriptModule.h>

struct Projetil : motor::Script {
    glm::vec3 velocidade{0.0f, 0.0f, -10.0f};  // metros por segundo, no mundo
    float vida = 2.0f;                         // segundos até sumir

    void OnUpdate(float dt) override {
        GetTransform().position += velocidade * dt;
        vida -= dt;
        if (vida <= 0.0f) DestroySelf();  // a entidade some no fim do frame (OnDestroy antes)
    }
};
