// Guia de scripts (docs/scripts.md), "Entrada e tempo" e "Achar, criar e destruir entidades": com F (ou o
// botão esquerdo do mouse) cria um projétil na frente da entidade, no máximo um a cada `intervalo` segundos.
// O projétil usa a mesma malha do Mesh Renderer desta entidade, em tamanho menor.

#include "Projetil.h"

#include <glm/geometric.hpp>

struct Disparador : motor::Script {
    float intervalo = 0.25f;  // segundos entre dois tiros
    float velocidade = 12.0f;
    float tamanho = 0.2f;

    void OnUpdate(float /*dt*/) override {
        const bool atirar = WasKeyPressed(motor::Key::F) || IsMouseDown(motor::MouseButton::Left);
        const double agora = Time().elapsed;
        if (!atirar || agora < m_proximoTiro) return;
        m_proximoTiro = agora + intervalo;

        // Posição e frente (-Z) no mundo, mesmo que esta entidade seja filha de outra.
        const glm::mat4 mundo = GetScene().WorldMatrix(GetId());
        const glm::vec3 posicao{mundo[3]};
        const glm::vec3 frente = -glm::normalize(glm::vec3(mundo[2]));

        // Use a referência só aqui; para lembrar da entidade depois, guarde o id (tiro.id).
        motor::Entity& tiro = CreateEntity("Projétil");
        tiro.transform.position = posicao + frente;
        tiro.transform.scale = glm::vec3(tamanho);
        if (const auto* meu = GetEntity().Get<motor::MeshRenderer>()) {
            tiro.Add<motor::MeshRenderer>() = *meu;  // copia malha e material
        }
        Projetil& projetil = tiro.Add<Projetil>();  // scripts também são componentes
        projetil.velocidade = frente * velocidade;
        Log("disparou {} (começa no próximo frame)", tiro.id);
    }

private:
    double m_proximoTiro = 0.0;
};

MOTOR_SCRIPT(Disparador) {
    registry.Register<Disparador>("Disparador", "Disparador")
        .Field("intervalo", "Intervalo", &Disparador::intervalo, {.speed = 0.01f, .min = 0.0f, .max = 10.0f, .format = "%.2f s"})
        .Field("velocidade", "Velocidade", &Disparador::velocidade, {.speed = 0.1f, .format = "%.1f m/s"})
        .Field("tamanho", "Tamanho", &Disparador::tamanho, {.speed = 0.01f, .min = 0.01f, .max = 10.0f});
}
