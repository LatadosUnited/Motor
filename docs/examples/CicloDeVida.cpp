// Guia de scripts (docs/scripts.md), "Ciclo de vida": escreve no log quando cada método é chamado.
// Ponha numa entidade, aperte Play e acompanhe o painel de log.

#include <motor/scene/ScriptModule.h>

struct CicloDeVida : motor::Script {
    int passosFixos = 0;  // contadores internos (não registrados)

    void OnStart() override {
        Log("OnStart: entidade {} começou (frame {})", GetId(), Time().frame);
    }

    void OnUpdate(float dt) override {
        // Todo frame. Para não encher o log, escreve uma vez a cada 60 frames.
        if (Time().frame % 60 == 0) {
            Log("OnUpdate: dt = {:.4f} s, {:.1f} s de jogo, {} passos fixos", dt, Time().elapsed, passosFixos);
        }
    }

    void OnFixedUpdate(float /*dt*/) override {
        ++passosFixos;  // 60 vezes por segundo, não importa o FPS
    }

    void OnCollisionEnter(const motor::Collision& c) override {
        Log("OnCollisionEnter: bateu em {} a {:.1f} m/s", c.other, c.relativeSpeed);
    }
    void OnCollisionExit(motor::EntityId outro) override { Log("OnCollisionExit: {}", outro); }
    void OnTriggerEnter(motor::EntityId outro) override { Log("OnTriggerEnter: {}", outro); }
    void OnTriggerExit(motor::EntityId outro) override { Log("OnTriggerExit: {}", outro); }

    void OnDestroy() override {
        Log("OnDestroy: fim depois de {:.1f} s", Time().elapsed);
    }
};

MOTOR_SCRIPT(CicloDeVida) {
    registry.Register<CicloDeVida>("CicloDeVida", "Ciclo de vida (log)");
}
