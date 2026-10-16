// Guia de scripts (docs/scripts.md), "O primeiro script": a entidade sobe e desce devagar, como uma boia.
// Copie para a pasta Scripts/ do seu projeto e adicione "Flutuar" a uma entidade no Inspector.

#include <motor/scene/ScriptModule.h>

#include <cmath>

struct Flutuar : motor::Script {
    float altura = 0.5f;      // metros acima e abaixo do ponto de partida
    float velocidade = 2.0f;  // quanto maior, mais rápido

    void OnStart() override {
        m_baseY = GetTransform().position.y;
        Log("começou a flutuar em y = {:.2f}", m_baseY);
    }

    void OnUpdate(float /*dt*/) override {
        const float t = static_cast<float>(Time().elapsed);  // segundos desde o Play
        GetTransform().position.y = m_baseY + altura * std::sin(t * velocidade);
    }

private:
    float m_baseY = 0.0f;  // não registrado: estado interno, não aparece no Inspector nem é salvo
};

MOTOR_SCRIPT(Flutuar) {
    registry.Register<Flutuar>("Flutuar", "Flutuar")
        .Field("altura", "Altura", &Flutuar::altura)
        .Field("velocidade", "Velocidade", &Flutuar::velocidade);
}
