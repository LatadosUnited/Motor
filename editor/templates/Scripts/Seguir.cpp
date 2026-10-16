// Segue outra entidade (achada pelo nome) mantendo um deslocamento; útil na câmera. A entidade que segue
// deve estar na raiz da hierarquia. Script de exemplo do Motor.
// Mostra como achar outra entidade e guardar o id dela (nunca o ponteiro: ela pode ser destruída).
// Guia passo a passo: docs/scripts.md na pasta do Motor (receita "Câmera que segue").

#include <motor/scene/ScriptModule.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>

struct Seguir : motor::Script {
    std::string alvo = "Jogador";
    glm::vec3 deslocamento{0.0f, 4.0f, 7.0f};
    float suavidade = 6.0f;  // 0 = instantâneo
    bool olhar = true;       // aponta para o alvo

    // OnStart roda uma vez, no início do Play: bom lugar para achar as outras entidades.
    void OnStart() override {
        const motor::Entity* target = FindEntity(alvo);
        m_alvo = target ? target->id : motor::kNullEntity;
        if (!target) Log("Seguir: entidade \"{}\" não encontrada", alvo);
    }

    void OnUpdate(float dt) override {
        motor::Scene& scene = GetScene();
        if (!scene.Find(m_alvo)) return;  // alvo destruído

        const glm::vec3 target{scene.WorldMatrix(m_alvo)[3]};
        motor::Transform& t = GetTransform();
        const glm::vec3 desired = target + deslocamento;
        // Aproximação exponencial: independe do FPS.
        const float k = suavidade > 0.0f ? 1.0f - std::exp(-suavidade * dt) : 1.0f;
        t.position += (desired - t.position) * k;

        const glm::vec3 look = target - t.position;
        if (olhar && glm::length(look) > 1e-4f) {
            t.rotation = glm::quatLookAt(glm::normalize(look), glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }

private:
    motor::EntityId m_alvo = motor::kNullEntity;  // estado interno: não registrado, não é salvo
};

MOTOR_SCRIPT(Seguir) {
    registry.Register<Seguir>("Seguir", "Seguir")
        .Field("alvo", "Alvo", &Seguir::alvo, {.tooltip = "Nome da entidade a seguir"})
        .Field("deslocamento", "Deslocamento", &Seguir::deslocamento)
        .Field("suavidade", "Suavidade", &Seguir::suavidade,
               {.speed = 0.1f, .min = 0.0f, .max = 50.0f, .tooltip = "0 = cola no alvo"})
        .Field("olhar", "Olhar o alvo", &Seguir::olhar);
}
