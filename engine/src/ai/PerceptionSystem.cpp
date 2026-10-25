#include "motor/ai/PerceptionSystem.h"

#include "motor/audio/AudioSystem.h"
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
    m_audio = runtime.GetSystem<AudioSystem>();
}

void PerceptionSystem::OnStop(SceneRuntime&) {
    m_seen.clear();
    m_heard.clear();
    m_memory.clear();
    m_los.clear();
    m_targets.clear();
    m_grid.clear();
    m_pending.clear();
    m_scene = nullptr;
    m_physics = nullptr;
    m_audio = nullptr;
}

void PerceptionSystem::OnFixedUpdate(SceneRuntime&, float dt) {
    if (!m_scene) return;
    const auto start = std::chrono::steady_clock::now();
    m_stats = {};
    ++m_step;

    // Os alvos do passo, com o ponto olhado já no mundo, e a grade deles (pelo ponto, no plano XZ).
    m_targets.clear();
    m_grid.clear();
    float maxVisibility = 0.0f;
    for (EntityId id : m_scene->EntitiesWith<PerceptionTarget>()) {
        if (!m_scene->IsActiveInHierarchy(id)) continue;
        const PerceptionTarget* t = m_scene->Find(id)->Get<PerceptionTarget>();
        const glm::vec3 point = glm::vec3(m_scene->WorldMatrix(id)[3]) + glm::vec3(0.0f, t->height, 0.0f);
        m_grid[CellKey(CellOf(point.x), CellOf(point.z))].push_back(static_cast<int>(m_targets.size()));
        m_targets.push_back({id, point, t});
        maxVisibility = std::max(maxVisibility, t->visibility);
    }
    m_stats.targets = static_cast<int>(m_targets.size());

    // Quem saiu da cena, perdeu a Percepção ou foi desligado não vê nem ouve mais nada (e esquece tudo).
    const auto gone = [&](const auto& entry) {
        const Entity* e = m_scene->Find(entry.first);
        return !e || !e->Get<Perception>() || !m_scene->IsActiveInHierarchy(entry.first);
    };
    std::erase_if(m_seen, gone);
    std::erase_if(m_heard, gone);
    std::erase_if(m_memory, gone);

    // 1. O barato: cada agente com os alvos das células que o alcance dele cobre, pelo alcance e pelo cone.
    std::vector<EntityId> agents;
    std::vector<Candidate> candidates;
    std::vector<int> nearby;
    for (EntityId agent : m_scene->EntitiesWith<Perception>()) {
        if (!m_scene->IsActiveInHierarchy(agent)) continue;
        agents.push_back(agent);
        const Perception& senses = *m_scene->Find(agent)->Get<Perception>();
        const PerceptionEye eye = PerceptionEyeOf(*m_scene, agent);
        const float reach = std::max(senses.nearRange, senses.sightRange * maxVisibility);
        GatherNearby(eye.position, reach, nearby);
        for (int index : nearby) {
            const TargetInfo& target = m_targets[index];
            if (target.id == agent || !senses.Detects(target.settings->group)) continue;
            ++m_stats.checks;
            float distance = 0.0f;
            if (!InView(senses, eye, target.point, target.settings->visibility, &distance)) continue;
            candidates.push_back({agent, index, distance, eye.position});
        }
    }
    m_stats.perceivers = static_cast<int>(agents.size());

    // 2. O caro: a linha de visada. Até o orçamento por passo; passando dele, os pares olhados há mais tempo (os
    // novos primeiro) ganham o raio, e os outros ficam com o resultado do último raio deles.
    std::vector<Candidate*> order(candidates.size());
    for (size_t i = 0; i < candidates.size(); ++i) order[i] = &candidates[i];
    for (Candidate* c : order) {
        const auto it = m_los.find(PairKey(c->agent, m_targets[c->target].id));
        c->lastRay = it == m_los.end() ? 0 : it->second.step;
    }
    const size_t rays = std::min(order.size(), static_cast<size_t>(std::max(0, m_raycastBudget)));
    if (rays < order.size()) {
        std::nth_element(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(rays), order.end(),
                         [](const Candidate* x, const Candidate* y) { return x->lastRay < y->lastRay; });
    }
    for (size_t i = 0; i < order.size(); ++i) {
        Candidate& c = *order[i];
        const TargetInfo& target = m_targets[c.target];
        LosEntry& entry = m_los[PairKey(c.agent, target.id)];
        if (i < rays) {
            entry.clear = LineOfSight(c.agent, c.eye, target.id, target.point, c.distance);
            entry.step = m_step;
            ++m_stats.raycasts;
        } else {
            ++m_stats.deferred;  // sem raio ainda (par novo): não vê até a vez dele
        }
        entry.used = m_step;
        c.clear = entry.step != 0 && entry.clear;
    }
    // Par que saiu do alcance ou do cone: o próximo encontro começa com um raio novo.
    std::erase_if(m_los, [&](const auto& entry) { return entry.second.used != m_step; });

    // 3. Visão, audição e memória de cada agente.
    std::vector<Noise> noises = std::move(m_pending);
    m_pending.clear();
    CollectSounds(noises);
    m_stats.noises = static_cast<int>(noises.size());
    for (EntityId agent : agents) m_seen[agent].clear();
    for (const Candidate& c : candidates)
        if (c.clear) m_seen[c.agent].push_back({m_targets[c.target].id, c.distance});
    for (EntityId agent : agents) {
        const Perception& senses = *m_scene->Find(agent)->Get<Perception>();
        std::vector<Sight>& seen = m_seen[agent];
        // O mais perto primeiro: é quase sempre o que o script quer.
        std::sort(seen.begin(), seen.end(), [](const Sight& x, const Sight& y) { return x.distance < y.distance; });
        Hear(agent, senses, noises);
        Remember(agent, senses, dt);
    }

    m_stats.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

int PerceptionSystem::CellOf(float coordinate) {
    return static_cast<int>(std::floor(coordinate / kCellSize));
}

uint64_t PerceptionSystem::CellKey(int x, int z) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(z);
}

uint64_t PerceptionSystem::PairKey(EntityId agent, EntityId target) {
    return (static_cast<uint64_t>(agent) << 32) | target;
}

void PerceptionSystem::GatherNearby(const glm::vec3& center, float reach, std::vector<int>& out) const {
    out.clear();
    const int x0 = CellOf(center.x - reach), x1 = CellOf(center.x + reach);
    const int z0 = CellOf(center.z - reach), z1 = CellOf(center.z + reach);
    // Alcance que cobre mais células que alvos: olhar a lista inteira sai mais barato.
    const double cells = (static_cast<double>(x1) - x0 + 1) * (static_cast<double>(z1) - z0 + 1);
    if (cells > static_cast<double>(m_targets.size())) {
        for (size_t i = 0; i < m_targets.size(); ++i) out.push_back(static_cast<int>(i));
        return;
    }
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            if (const auto it = m_grid.find(CellKey(x, z)); it != m_grid.end())
                out.insert(out.end(), it->second.begin(), it->second.end());
}

bool PerceptionSystem::InView(const Perception& senses, const PerceptionEye& eye, const glm::vec3& point,
                              float visibility, float* distance) {
    if (visibility <= 0.0f) return false;
    const glm::vec3 toTarget = point - eye.position;
    const float d = glm::length(toTarget);
    *distance = d;
    if (d <= senses.nearRange) return true;  // perto: qualquer direção
    if (d > senses.sightRange * visibility) return false;
    if (senses.sightAngle >= 360.0f) return true;
    if (d < 1e-6f) return true;
    const float halfAngle = glm::radians(std::max(0.0f, senses.sightAngle) * 0.5f);
    return glm::dot(eye.forward, toTarget / d) >= std::cos(halfAngle);
}

bool PerceptionSystem::LineOfSight(EntityId agent, const glm::vec3& eye, EntityId target, const glm::vec3& point,
                                   float distance) const {
    if (!m_physics || distance < 1e-4f) return true;
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

void PerceptionSystem::CollectSounds(std::vector<Noise>& noises) const {
    if (!m_audio) return;
    for (EntityId id : m_scene->EntitiesWith<PerceptionTarget>()) {
        const Entity* e = m_scene->Find(id);
        const AudioSource* source = e->Get<AudioSource>();
        const float range = e->Get<PerceptionTarget>()->soundRange * (source ? source->volume : 0.0f);
        if (range <= 0.0f || !m_scene->IsActiveInHierarchy(id) || !m_audio->IsPlaying(id)) continue;
        noises.push_back({glm::vec3(m_scene->WorldMatrix(id)[3]), range, id});
    }
}

void PerceptionSystem::Hear(EntityId agent, const Perception& senses, const std::vector<Noise>& noises) {
    std::vector<HeardNoise>& heard = m_heard[agent];
    heard.clear();
    if (senses.hearing <= 0.0f || noises.empty()) return;
    const glm::vec3 ears = PerceptionEyeOf(*m_scene, agent).position;
    for (const Noise& noise : noises) {
        if (noise.source != kNullEntity) {
            if (IsPart(*m_scene, noise.source, agent)) continue;  // os próprios passos
            const Entity* from = m_scene->Find(noise.source);
            const PerceptionTarget* target = from ? from->Get<PerceptionTarget>() : nullptr;
            if (target && !senses.Detects(target->group)) continue;
        }
        const float distance = glm::distance(ears, noise.position);
        if (distance > noise.range * senses.hearing) continue;
        // De cada fonte, só o mais perto (o passo e o som dele no mesmo passo contam uma vez).
        const auto same = std::find_if(heard.begin(), heard.end(), [&](const HeardNoise& h) {
            return noise.source != kNullEntity && h.source == noise.source;
        });
        if (same == heard.end()) heard.push_back({noise.source, noise.position, distance});
        else if (distance < same->distance) *same = {noise.source, noise.position, distance};
    }
    std::sort(heard.begin(), heard.end(),
              [](const HeardNoise& a, const HeardNoise& b) { return a.distance < b.distance; });
}

void PerceptionSystem::Remember(EntityId agent, const Perception& senses, float dt) {
    std::vector<PerceptionMemory>& memory = m_memory[agent];
    for (PerceptionMemory& m : memory) {
        m.age += dt;
        m.seen = m.heard = false;
    }
    const auto entry = [&](EntityId target) -> PerceptionMemory& {
        const auto it = std::find_if(memory.begin(), memory.end(),
                                     [&](const PerceptionMemory& m) { return m.target == target; });
        if (it != memory.end()) return *it;
        memory.push_back({});
        memory.back().target = target;
        return memory.back();
    };
    // Ouvido primeiro: se também foi visto, a posição de quem viu é a que vale.
    for (const HeardNoise& noise : m_heard[agent]) {
        if (noise.source == kNullEntity) continue;
        PerceptionMemory& m = entry(noise.source);
        m.position = noise.position;
        m.age = 0.0f;
        m.heard = true;
        m.bySight = false;
    }
    for (const Sight& sight : m_seen[agent]) {
        PerceptionMemory& m = entry(sight.target);
        m.position = glm::vec3(m_scene->WorldMatrix(sight.target)[3]);
        m.age = 0.0f;
        m.seen = true;
        m.bySight = true;
    }
    // Esquece quem passou da memória sem ser percebido e quem saiu da cena.
    std::erase_if(memory, [&](const PerceptionMemory& m) {
        return m.age > senses.memory || !m_scene->Find(m.target);
    });
    const glm::vec3 eye = PerceptionEyeOf(*m_scene, agent).position;
    std::sort(memory.begin(), memory.end(), [&](const PerceptionMemory& a, const PerceptionMemory& b) {
        if (a.age != b.age) return a.age < b.age;
        return glm::distance(eye, a.position) < glm::distance(eye, b.position);
    });
}

std::vector<PerceptionMemory> PerceptionSystem::GetMemory(EntityId agent) const {
    const auto it = m_memory.find(agent);
    return it == m_memory.end() ? std::vector<PerceptionMemory>{} : it->second;
}

std::optional<PerceptionMemory> PerceptionSystem::Recall(EntityId agent, EntityId target) const {
    const auto it = m_memory.find(agent);
    if (it == m_memory.end()) return std::nullopt;
    for (const PerceptionMemory& m : it->second)
        if (m.target == target) return m;
    return std::nullopt;
}

void PerceptionSystem::Forget(EntityId agent, EntityId target) {
    const auto it = m_memory.find(agent);
    if (it == m_memory.end()) return;
    if (target == kNullEntity) it->second.clear();
    else std::erase_if(it->second, [&](const PerceptionMemory& m) { return m.target == target; });
}

void PerceptionSystem::MakeNoise(const glm::vec3& position, float range, EntityId source) {
    if (range > 0.0f) m_pending.push_back({position, range, source});
}

std::vector<HeardNoise> PerceptionSystem::GetHeard(EntityId agent) const {
    const auto it = m_heard.find(agent);
    return it == m_heard.end() ? std::vector<HeardNoise>{} : it->second;
}

bool PerceptionSystem::CanHear(EntityId agent, EntityId source) const {
    const auto it = m_heard.find(agent);
    if (it == m_heard.end()) return false;
    return std::any_of(it->second.begin(), it->second.end(), [&](const HeardNoise& h) { return h.source == source; });
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
    const Perception& senses = *a->Get<Perception>();
    const PerceptionTarget& seen = *t->Get<PerceptionTarget>();
    if (!senses.Detects(seen.group)) return false;
    const PerceptionEye eye = PerceptionEyeOf(*m_scene, agent);
    const glm::vec3 point = PerceptionPointOf(*m_scene, target);
    float distance = 0.0f;
    return InView(senses, eye, point, seen.visibility, &distance) &&
           LineOfSight(agent, eye.position, target, point, distance);
}

void PerceptionSystem::SetRaycastBudget(int raysPerStep) {
    m_raycastBudget = std::max(1, raysPerStep);
}

MOTOR_RUNTIME_SYSTEM(Percepcao, PerceptionSystem, kSystemOrderPerception);

} // namespace motor
