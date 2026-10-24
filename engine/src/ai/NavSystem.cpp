#include "motor/ai/NavSystem.h"

#include "motor/core/Log.h"
#include "motor/scene/Components.h"
#include "motor/scene/SceneRuntime.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace motor {

namespace {

// Distância (no plano) em que um canto do caminho conta como alcançado.
constexpr float kCornerReach = 0.05f;

glm::vec3 Flat(const glm::vec3& v) { return {v.x, 0.0f, v.z}; }

glm::vec3 MoveTowards(const glm::vec3& from, const glm::vec3& to, float maxDelta) {
    const glm::vec3 delta = to - from;
    const float length = glm::length(delta);
    if (length <= maxDelta || length < 1e-6f) return to;
    return from + delta * (maxDelta / length);
}

// Primeira NavMeshSurface ativa da cena.
const NavMeshSurface* FindSurface(const Scene& scene) {
    for (EntityId id : scene.EntitiesWith<NavMeshSurface>()) {
        const Entity* e = scene.Find(id);
        if (e && scene.IsActiveInHierarchy(id)) return e->Get<NavMeshSurface>();
    }
    return nullptr;
}

NavMeshBuildSettings SettingsFrom(const NavMeshSurface& s) {
    NavMeshBuildSettings b;
    b.agentRadius = s.agentRadius;
    b.agentHeight = s.agentHeight;
    b.agentMaxClimb = s.agentMaxClimb;
    b.agentMaxSlope = s.agentMaxSlope;
    b.cellSize = s.cellSize;
    b.cellHeight = s.cellHeight;
    b.minRegionArea = s.minRegionArea;
    return b;
}

} // namespace

NavSystem::NavSystem() = default;
NavSystem::~NavSystem() = default;

void NavSystem::OnStart(SceneRuntime& runtime) {
    m_scene = &runtime.GetScene();
    if (FindSurface(*m_scene)) Rebuild();
}

void NavSystem::OnStop(SceneRuntime&) {
    m_agents.clear();
    m_navMesh.reset();
    m_scene = nullptr;
}

bool NavSystem::Rebuild() {
    if (!m_scene) return false;
    m_stats = {};
    const NavMeshSurface* surface = FindSurface(*m_scene);
    if (!surface) {
        m_navMesh.reset();
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();
    const NavGeometry geometry = CollectNavGeometry(*m_scene);
    m_stats.triangles = static_cast<int>(geometry.TriangleCount());
    m_navMesh = NavMesh::Build(geometry, SettingsFrom(*surface), &m_stats.error);
    m_stats.buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    if (!m_navMesh) {
        log::Warn("Navegação: navmesh não gerado: {}", m_stats.error);
        for (auto& [id, agent] : m_agents) agent.corners.clear();
        return false;
    }
    m_stats.polygons = m_navMesh->PolygonCount();
    log::Info("Navegação: navmesh com {} polígonos ({} triângulos de entrada) em {:.1f} ms", m_stats.polygons,
              m_stats.triangles, m_stats.buildMs);

    // O chão mudou: quem estava andando refaz o caminho até o mesmo destino.
    for (auto& [id, agent] : m_agents) {
        if (!agent.hasDestination || agent.arrived) continue;
        if (const Entity* e = m_scene->Find(id); e && e->Has<NavMeshAgent>()) {
            const glm::vec3 position = glm::vec3(m_scene->WorldMatrix(id)[3]) - glm::vec3(0.0f, e->Get<NavMeshAgent>()->baseOffset, 0.0f);
            PlanPath(agent, position, agent.destination);
        }
    }
    return true;
}

bool NavSystem::FindPath(const glm::vec3& start, const glm::vec3& end, std::vector<glm::vec3>& corners) const {
    corners.clear();
    return m_navMesh && m_navMesh->FindPath(start, end, corners);
}

std::optional<glm::vec3> NavSystem::SamplePosition(const glm::vec3& position, float maxDistance) const {
    if (!m_navMesh) return std::nullopt;
    const auto point = m_navMesh->SamplePosition(position, glm::vec3(maxDistance));
    if (point && glm::distance(*point, position) > maxDistance) return std::nullopt;  // a caixa é maior que a esfera
    return point;
}

std::optional<NavRaycastHit> NavSystem::Raycast(const glm::vec3& start, const glm::vec3& end) const {
    if (!m_navMesh) return std::nullopt;
    return m_navMesh->Raycast(start, end);
}

NavSystem::Agent* NavSystem::FindAgent(EntityId id) {
    auto it = m_agents.find(id);
    return it == m_agents.end() ? nullptr : &it->second;
}

const NavSystem::Agent* NavSystem::FindAgent(EntityId id) const {
    auto it = m_agents.find(id);
    return it == m_agents.end() ? nullptr : &it->second;
}

NavSystem::Agent& NavSystem::AgentFor(EntityId id) {
    Agent& agent = m_agents[id];
    agent.seen = m_step;
    return agent;
}

bool NavSystem::PlanPath(Agent& agent, const glm::vec3& from, const glm::vec3& destination) {
    agent.destination = destination;
    agent.hasDestination = true;
    agent.arrived = false;
    agent.partial = false;
    agent.next = 0;
    agent.corners.clear();
    if (!m_navMesh || !m_navMesh->FindPath(from, destination, agent.corners, &agent.partial)) {
        agent.corners.clear();
        return false;
    }
    // O primeiro canto é onde o agente já está.
    agent.next = agent.corners.size() > 1 ? 1 : 0;
    return true;
}

bool NavSystem::SetDestination(EntityId id, const glm::vec3& destination) {
    const Entity* e = m_scene ? m_scene->Find(id) : nullptr;
    const NavMeshAgent* settings = e ? e->Get<NavMeshAgent>() : nullptr;
    if (!settings) return false;
    Agent& agent = AgentFor(id);
    agent.followingTarget = false;
    const glm::vec3 position = glm::vec3(m_scene->WorldMatrix(id)[3]) - glm::vec3(0.0f, settings->baseOffset, 0.0f);
    return PlanPath(agent, position, destination);
}

void NavSystem::Stop(EntityId id) {
    if (Agent* agent = FindAgent(id)) {
        agent->hasDestination = false;
        agent->arrived = false;
        agent->followingTarget = false;
        agent->corners.clear();
    }
}

bool NavSystem::HasPath(EntityId id) const {
    const Agent* agent = FindAgent(id);
    return agent && agent->hasDestination && !agent->arrived && !agent->corners.empty();
}

bool NavSystem::HasArrived(EntityId id) const {
    const Agent* agent = FindAgent(id);
    return agent && agent->arrived;
}

bool NavSystem::IsPathPartial(EntityId id) const {
    const Agent* agent = FindAgent(id);
    return agent && agent->partial;
}

float NavSystem::RemainingDistance(EntityId id) const {
    const Agent* agent = FindAgent(id);
    if (!agent || !HasPath(id) || !m_scene) return 0.0f;
    const Entity* e = m_scene->Find(id);
    const float offset = e && e->Has<NavMeshAgent>() ? e->Get<NavMeshAgent>()->baseOffset : 0.0f;
    const glm::vec3 position = glm::vec3(m_scene->WorldMatrix(id)[3]) - glm::vec3(0.0f, offset, 0.0f);
    float distance = glm::distance(position, agent->corners[agent->next]);
    for (size_t i = agent->next + 1; i < agent->corners.size(); ++i) {
        distance += glm::distance(agent->corners[i - 1], agent->corners[i]);
    }
    return distance;
}

glm::vec3 NavSystem::GetVelocity(EntityId id) const {
    const Agent* agent = FindAgent(id);
    return agent ? agent->velocity : glm::vec3(0.0f);
}

std::vector<glm::vec3> NavSystem::GetPath(EntityId id) const {
    const Agent* agent = FindAgent(id);
    if (!agent || !HasPath(id)) return {};
    return {agent->corners.begin() + static_cast<std::ptrdiff_t>(agent->next), agent->corners.end()};
}

bool NavSystem::Warp(EntityId id, const glm::vec3& position) {
    Entity* e = m_scene ? m_scene->Find(id) : nullptr;
    const NavMeshAgent* settings = e ? e->Get<NavMeshAgent>() : nullptr;
    if (!settings || !m_navMesh) return false;
    const auto point = m_navMesh->SamplePosition(position);
    if (!point) return false;
    glm::mat4 world = m_scene->WorldMatrix(id);
    world[3] = glm::vec4(*point + glm::vec3(0.0f, settings->baseOffset, 0.0f), 1.0f);
    m_scene->SetWorldMatrix(id, world);
    Agent& agent = AgentFor(id);
    agent = Agent{};
    agent.seen = m_step;
    return true;
}

void NavSystem::OnFixedUpdate(SceneRuntime&, float dt) {
    if (!m_scene) return;
    ++m_step;
    // Cópia: um agente pode sumir no meio (não aqui, mas a lista muda se a estrutura da cena mudar).
    const std::vector<EntityId> agents = m_scene->EntitiesWith<NavMeshAgent>();
    if (!agents.empty() && !m_navMesh && !m_warnedNoNavMesh) {
        m_warnedNoNavMesh = true;
        log::Warn("Navegação: há agentes mas não há navmesh (falta uma Superfície de navegação na cena?)");
    }
    for (EntityId id : agents) {
        if (!m_scene->IsActiveInHierarchy(id)) continue;
        MoveAgent(id, AgentFor(id), dt);
    }
    // Agentes que sumiram (entidade destruída, componente removido) saem do registro.
    std::erase_if(m_agents, [&](const auto& item) { return item.second.seen != m_step; });
}

void NavSystem::MoveAgent(EntityId id, Agent& agent, float dt) {
    Entity* e = m_scene->Find(id);
    const NavMeshAgent& s = *e->Get<NavMeshAgent>();
    glm::mat4 world = m_scene->WorldMatrix(id);
    const glm::vec3 lift(0.0f, s.baseOffset, 0.0f);
    glm::vec3 position = glm::vec3(world[3]) - lift;

    // Alvo: refaz o caminho quando ele andou o bastante desde o último.
    if (!s.target.IsEmpty() && m_navMesh) {
        if (m_scene->Find(s.target.id) && m_scene->IsActiveInHierarchy(s.target.id)) {
            const glm::vec3 target = glm::vec3(m_scene->WorldMatrix(s.target.id)[3]);
            if (!agent.followingTarget || glm::distance(target, agent.targetSeen) > s.repathDistance ||
                (agent.corners.empty() && !agent.arrived)) {
                PlanPath(agent, position, target);
                agent.followingTarget = true;
                agent.targetSeen = target;
            }
        }
    }

    glm::vec3 desired(0.0f);
    if (agent.hasDestination && !agent.arrived && !agent.corners.empty()) {
        // Pula os cantos já alcançados (no plano: a altura do canto e a dos pés podem diferir um pouco).
        while (agent.next + 1 < agent.corners.size() &&
               glm::length(Flat(agent.corners[agent.next] - position)) <= kCornerReach) {
            ++agent.next;
        }
        float remaining = glm::length(Flat(agent.corners[agent.next] - position));
        for (size_t i = agent.next + 1; i < agent.corners.size(); ++i) {
            remaining += glm::length(Flat(agent.corners[i] - agent.corners[i - 1]));
        }
        if (remaining <= std::max(s.stoppingDistance, kCornerReach)) {
            agent.arrived = true;
            agent.corners.clear();
        } else {
            // Freia a tempo de parar no fim: v² = 2·a·d.
            const float speed = std::min(s.speed, std::sqrt(2.0f * s.acceleration * remaining));
            const glm::vec3 toCorner = Flat(agent.corners[agent.next] - position);
            const float length = glm::length(toCorner);
            if (length > 1e-5f) desired = toCorner / length * speed;
        }
    }
    agent.velocity = MoveTowards(agent.velocity, desired, s.acceleration * dt);
    if (glm::length(agent.velocity) < 1e-4f) {
        agent.velocity = glm::vec3(0.0f);
        return;
    }

    // Anda no plano sem passar do canto em que mira (passar dele faria o agente rodear o canto); a altura vem do
    // navmesh embaixo do ponto novo.
    glm::vec3 step = agent.velocity * dt;
    if (!agent.corners.empty()) {
        const float toCorner = glm::length(Flat(agent.corners[agent.next] - position));
        const float stepLength = glm::length(step);
        if (stepLength > toCorner) step *= toCorner / stepLength;
    }
    glm::vec3 next = position + step;
    if (m_navMesh) {
        const float reach = m_navMesh->Settings().agentMaxClimb + 0.5f;
        if (const auto ground = m_navMesh->SamplePosition(next, glm::vec3(0.5f, reach, 0.5f))) next = *ground;
    }

    // Vira só em Y, no máximo angularSpeed por segundo. A frente das entidades é -Z.
    glm::vec3 scale(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
    scale = glm::max(scale, glm::vec3(1e-6f));
    glm::quat rotation = glm::quat_cast(glm::mat3(glm::vec3(world[0]) / scale.x, glm::vec3(world[1]) / scale.y,
                                                  glm::vec3(world[2]) / scale.z));
    const glm::vec3 flatVelocity = Flat(agent.velocity);
    if (s.updateRotation && glm::length(flatVelocity) > 0.01f) {
        const glm::quat facing =
            glm::angleAxis(std::atan2(-flatVelocity.x, -flatVelocity.z), glm::vec3(0.0f, 1.0f, 0.0f));
        const float angle = glm::angle(rotation * glm::inverse(facing));
        const float maxTurn = glm::radians(s.angularSpeed) * dt;
        rotation = angle <= maxTurn || angle < 1e-4f ? facing : glm::slerp(rotation, facing, maxTurn / angle);
    }

    world = glm::translate(glm::mat4(1.0f), next + lift) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
    m_scene->SetWorldMatrix(id, world);
}

MOTOR_RUNTIME_SYSTEM(Navegacao, NavSystem, kSystemOrderNavigation);

} // namespace motor
