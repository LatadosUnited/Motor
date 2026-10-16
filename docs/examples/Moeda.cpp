// Guia de scripts (docs/scripts.md), receita "Coletável com pontuação": a moeda gira no lugar e, quando o
// jogador encosta, soma pontos no Placar e some.
// Na entidade da moeda: Mesh Renderer (o visual), Collider com "Trigger" marcado e este script.
// O jogador é quem tem o componente "Controlador de personagem" (ver o modelo Jogador).

#include "Placar.h"

#include <glm/gtc/quaternion.hpp>

struct Moeda : motor::Script {
    int valor = 1;
    float giro = 180.0f;  // graus por segundo, em torno do Y

    void OnStart() override {
        if (Placar* placar = AcharPlacar(GetScene())) {
            placar->total += valor;
        } else {
            Log("sem placar: crie uma entidade \"Placar\" com o script Placar");
        }
    }

    void OnUpdate(float dt) override {
        GetTransform().Rotate(glm::vec3(0.0f, 1.0f, 0.0f), glm::radians(giro * dt));
    }

    void OnTriggerEnter(motor::EntityId outro) override {
        if (m_coletada || !EhJogador(outro)) return;
        m_coletada = true;  // DestroySelf só acontece no fim do passo: não conta duas vezes
        if (Placar* placar = AcharPlacar(GetScene())) placar->Somar(valor);
        DestroySelf();
    }

private:
    bool EhJogador(motor::EntityId id) {
        const motor::Entity* entidade = GetScene().Find(id);
        return entidade && entidade->Has<motor::CharacterControllerComponent>();
    }

    bool m_coletada = false;
};

MOTOR_SCRIPT(Moeda) {
    registry.Register<Moeda>("Moeda", "Moeda (coletável)")
        .Field("valor", "Valor", &Moeda::valor, {.min = 0.0f, .max = 1000.0f})
        .Field("giro", "Giro", &Moeda::giro, {.speed = 1.0f, .format = "%.0f°/s"});
}
