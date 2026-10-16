// Guia de scripts (docs/scripts.md), receita "Inimigo que persegue": anda até o alvo quando o vê (raycast
// sem nada no meio) e, se chegar perto, manda o alvo de volta para o ponto de partida.
// Na entidade do inimigo: Mesh Renderer, "Controlador de personagem" e este script. O alvo (o jogador)
// também tem Controlador de personagem.

#include <motor/physics/PhysicsSystem.h>
#include <motor/scene/ScriptModule.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>

struct Inimigo : motor::Script {
    std::string alvo = "Jogador";
    float velocidade = 2.5f;  // m/s
    float visao = 12.0f;      // até onde enxerga, em metros
    float alcance = 1.0f;     // distância para "pegar" o alvo
    glm::vec3 recomeco{0.0f, 1.0f, 0.0f};  // para onde o alvo volta quando é pego

    void OnStart() override {
        const motor::Entity* entidade = FindEntity(alvo);
        m_alvo = entidade ? entidade->id : motor::kNullEntity;
        if (!entidade) Log("alvo \"{}\" não encontrado", alvo);
    }

    void OnFixedUpdate(float /*dt*/) override {
        auto* fisica = GetSystem<motor::PhysicsSystem>();
        motor::Scene& scene = GetScene();
        if (!fisica || !scene.Find(m_alvo)) return;

        const glm::vec3 eu{scene.WorldMatrix(GetId())[3]};
        const glm::vec3 ele{scene.WorldMatrix(m_alvo)[3]};
        glm::vec3 direcao = ele - eu;
        direcao.y = 0.0f;  // anda só no plano
        const float distancia = glm::length(direcao);

        if (distancia < alcance) {
            Log("peguei!");
            scene.Find(m_alvo)->transform.position = recomeco;  // teleporta o personagem
            fisica->MoveCharacter(GetId(), glm::vec3(0.0f));
            return;
        }

        // Enxerga se o primeiro corpo na direção do alvo é o próprio alvo (o raio ignora o inimigo).
        const auto hit = fisica->Raycast(eu, ele - eu, visao, GetId());
        if (!hit || hit->entity != m_alvo) {
            fisica->MoveCharacter(GetId(), glm::vec3(0.0f));  // parado até ver de novo
            return;
        }

        direcao /= distancia;
        fisica->MoveCharacter(GetId(), direcao * velocidade);
        // Vira a frente (-Z) para o alvo.
        GetTransform().rotation = glm::angleAxis(std::atan2(-direcao.x, -direcao.z), glm::vec3(0.0f, 1.0f, 0.0f));
    }

private:
    motor::EntityId m_alvo = motor::kNullEntity;
};

MOTOR_SCRIPT(Inimigo) {
    registry.Register<Inimigo>("Inimigo", "Inimigo (persegue)")
        .Field("alvo", "Alvo", &Inimigo::alvo, {.tooltip = "Nome da entidade perseguida"})
        .Field("velocidade", "Velocidade", &Inimigo::velocidade, {.speed = 0.1f, .min = 0.0f, .max = 50.0f, .format = "%.1f m/s"})
        .Field("visao", "Visão", &Inimigo::visao, {.speed = 0.1f, .min = 0.0f, .max = 1000.0f, .format = "%.1f m"})
        .Field("alcance", "Alcance", &Inimigo::alcance, {.speed = 0.05f, .min = 0.0f, .max = 100.0f, .format = "%.2f m"})
        .Field("recomeco", "Recomeço do alvo", &Inimigo::recomeco);
}
