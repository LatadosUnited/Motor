#include "scripts/Scripts.h"

#include <motor/scene/Scene.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace scripts {

void Seguir::OnStart() {
    const motor::Entity* target = FindEntity(alvo);
    m_alvo = target ? target->id : motor::kNullEntity;
    if (!target) Log("Seguir: entidade \"{}\" não encontrada", alvo);
}

void Seguir::OnUpdate(float dt) {
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

} // namespace scripts
