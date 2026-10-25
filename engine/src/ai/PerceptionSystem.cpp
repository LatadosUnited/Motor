#include "motor/ai/PerceptionSystem.h"

#include "motor/physics/PhysicsSystem.h"
#include "motor/scene/Components.h"
#include "motor/scene/SceneRuntime.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace motor {

namespace {

// Folga no fim da linha de visada: um Collider encostado no ponto olhado (o chão sob os pés) não tapa.
constexpr float kSightSlack = 0.05f;

bool IsPart(const Scene& scene, EntityId hit, EntityId owner) {
    return hit == owner || scene.IsDescendantOf(hit, owner);
}

} // namespace

PerceptionEye PerceptionEyeOf(const Scene& scene, EntityId id) {
    PerceptionEye eye;
    const Entity* e = scene.Find(id);
    const Perception* p = e ? e->Get<Perception>() : nullptr;
    const glm::mat4 world = scene.WorldMatrix(id);
    eye.position = glm::vec3(world[3]) + glm::vec3(0.0f, p ? p->eyeHeight : 0.0f, 0.0f);
    const glm::vec3 forward = -glm::vec3(world[2]);
    const float length = glm::length(forward);
    if (length > 1e-6f) eye.forward = forward / length;
    return eye;
}

glm::vec3 PerceptionPointOf(const Scene& scene, EntityId id) {
    const Entity* e = scene.Find(id);
    const PerceptionTarget* t = e ? e->Get<PerceptionTarget>() : nullptr;
    return glm::vec3(scene.WorldMatrix(id)[3]) + glm::vec3(0.0f, t ? t->height : 0.0f, 0.0f);
}

void PerceptionSystem::OnStart(SceneRuntime& runtime) {
    m_scene = &runtime.GetScene();
    m_physics = runtime.GetSystem<PhysicsSystem>();
}

void PerceptionSystem::OnStop(SceneRuntime&) {
    m_seen.clear();
    m_scene = nullptr;
    m_physics = nullptr;
}

void PerceptionSystem::OnFixedUpdate(SceneRuntime&, float) {
    if (!m_scene) return;
    const auto start = std::chrono::steady_clock::now();
    m_stats = {};

    std::vector<EntityId> targets;
    for (EntityId id : m_scene->EntitiesWith<PerceptionTarget>())
        if (m_scene->IsActiveInHierarchy(id)) targets.push_back(id);
    m_stats.targets = static_cast<int>(targets.size());

    // Quem saiu da cena, perdeu a Percepção ou foi desligado não vê mais nada.
    for (auto it = m_seen.begin(); it != m_seen.end();) {
        const Entity* e = m_scene->Find(it->first);
        if (!e || !e->Get<Perception>() || !m_scene->IsActiveInHierarchy(it->first)) it = m_seen.erase(it);
        else ++it;
    }

    for (EntityId agent : m_scene->EntitiesWith<Perception>()) {
        if (!m_scene->IsActiveInHierarchy(agent)) continue;
        ++m_stats.perceivers;
        const Perception& senses = *m_scene->Find(agent)->Get<Perception>();
        std::vector<Sight>& seen = m_seen[agent];
        seen.clear();
        for (EntityId target : targets) {
            if (target == agent) continue;
            if (!senses.Detects(m_scene->Find(target)->Get<PerceptionTarget>()->group)) continue;
            ++m_stats.checks;
            float distance = 0.0f;
            if (!InView(agent, target, &distance)) continue;
            ++m_stats.raycasts;
            if (LineOfSight(agent, target, distance)) seen.push_back({target, distance});
        }
        // O mais perto primeiro: é quase sempre o que o script quer.
        std::sort(seen.begin(), seen.end(), [](const Sight& a, const Sight& b) { return a.distance < b.distance; });
    }

    m_stats.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

bool PerceptionSystem::InView(EntityId agent, EntityId target, float* distance) const {
    const Perception& senses = *m_scene->Find(agent)->Get<Perception>();
    const PerceptionTarget& seen = *m_scene->Find(target)->Get<PerceptionTarget>();
    if (seen.visibility <= 0.0f) return false;

    const PerceptionEye eye = PerceptionEyeOf(*m_scene, agent);
    const glm::vec3 toTarget = PerceptionPointOf(*m_scene, target) - eye.position;
    const float d = glm::length(toTarget);
    *distance = d;
    if (d <= senses.nearRange) return true;  // perto: qualquer direção
    if (d > senses.sightRange * seen.visibility) return false;
    if (senses.sightAngle >= 360.0f) return true;
    if (d < 1e-6f) return true;
    const float halfAngle = glm::radians(std::max(0.0f, senses.sightAngle) * 0.5f);
    return glm::dot(eye.forward, toTarget / d) >= std::cos(halfAngle);
}

bool PerceptionSystem::LineOfSight(EntityId agent, EntityId target, float distance) const {
    if (!m_physics || distance < 1e-4f) return true;
    const glm::vec3 eye = PerceptionEyeOf(*m_scene, agent).position;
    const glm::vec3 point = PerceptionPointOf(*m_scene, target);
    const glm::vec3 direction = (point - eye) / distance;
    const auto hit = m_physics->Raycast(eye, direction, distance, agent);
    if (!hit || hit->distance >= distance - kSightSlack) return true;
    if (IsPart(*m_scene, hit->entity, target)) return true;
    if (!IsPart(*m_scene, hit->entity, agent)) return false;
    // Bateu num Collider filho de quem olha (uma arma, um escudo): o raio só ignora uma entidade, então olha do alvo
    // para os olhos. O primeiro que ele encontra sendo de quem olha (ou nada) quer dizer que nada no meio tapa.
    const auto back = m_physics->Raycast(point, -direction, distance, target);
    if (!back || back->distance >= distance - kSightSlack) return true;
    return IsPart(*m_scene, back->entity, agent) || IsPart(*m_scene, back->entity, target);
}

std::vector<EntityId> PerceptionSystem::GetSeen(EntityId agent) const {
    std::vector<EntityId> result;
    const auto it = m_seen.find(agent);
    if (it == m_seen.end()) return result;
    result.reserve(it->second.size());
    for (const Sight& s : it->second) result.push_back(s.target);
    return result;
}

bool PerceptionSystem::CanSee(EntityId agent, EntityId target) const {
    const auto it = m_seen.find(agent);
    if (it == m_seen.end()) return false;
    return std::any_of(it->second.begin(), it->second.end(), [&](const Sight& s) { return s.target == target; });
}

bool PerceptionSystem::CheckSight(EntityId agent, EntityId target) const {
    if (!m_scene || agent == target) return false;
    const Entity* a = m_scene->Find(agent);
    const Entity* t = m_scene->Find(target);
    if (!a || !t || !a->Get<Perception>() || !t->Get<PerceptionTarget>()) return false;
    if (!m_scene->IsActiveInHierarchy(agent) || !m_scene->IsActiveInHierarchy(target)) return false;
    if (!a->Get<Perception>()->Detects(t->Get<PerceptionTarget>()->group)) return false;
    float distance = 0.0f;
    return InView(agent, target, &distance) && LineOfSight(agent, target, distance);
}

MOTOR_RUNTIME_SYSTEM(Percepcao, PerceptionSystem, kSystemOrderPerception);

} // namespace motor
