#include "motor/ai/NavSystem.h"

#include "NavCrowd.h"

#include "motor/core/Log.h"
#include "motor/scene/Components.h"
#include "motor/scene/SceneRuntime.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace motor {

namespace {

// Distância do destino em que o agente conta como chegado, mesmo com distância de parada 0.
constexpr float kArriveReach = 0.05f;
// A entidade saiu de onde o NavSystem a pôs por mais que isso (script, teleporte, pai que andou): a multidão a põe
// no lugar novo.
constexpr float kMovedOutside = 0.01f;
// Sem chegar `kProgress` mais perto do destino por `kBlockedTime` (perto dele, ou encostado num agente parado): chegou
// o mais perto que dava.
constexpr float kProgress = 0.05f;
constexpr float kBlockedTime = 0.5f;

glm::vec3 Flat(const glm::vec3& v) { return {v.x, 0.0f, v.z}; }

// Primeira NavMeshSurface ativa da cena.
const NavMeshSurface* FindSurface(const Scene& scene) {
    for (EntityId id : scene.EntitiesWith<NavMeshSurface>()) {
        const Entity* e = scene.Find(id);
        if (e && scene.IsActiveInHierarchy(id)) return e->Get<NavMeshSurface>();
    }
    return nullptr;
}

NavCrowd::AgentParams CrowdParams(const NavMeshAgent& s) {
    NavCrowd::AgentParams p;
    p.radius = s.radius;
    p.height = s.height;
    p.speed = s.speed;
    p.acceleration = s.acceleration;
    p.avoidOthers = s.avoidOthers;
    return p;
}

} // namespace

NavMeshBuildSettings NavSettingsFrom(const NavMeshSurface& s) {
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

std::unique_ptr<NavMesh> BuildSceneNavMesh(const Scene& scene, const NavMeshSurface& surface, NavStats* stats) {
    NavStats local;
    NavStats& out = stats ? *stats : local;
    out = {};
    const auto t0 = std::chrono::steady_clock::now();
    const NavGeometry geometry = CollectNavGeometry(scene);
    out.triangles = static_cast<int>(geometry.TriangleCount());
    auto nav = NavMesh::Build(geometry, NavSettingsFrom(surface), &out.error);
    out.buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (nav) out.polygons = nav->PolygonCount();
    return nav;
}

NavSystem::NavSystem() = default;
NavSystem::~NavSystem() = default;

void NavSystem::OnStart(SceneRuntime& runtime) {
    m_scene = &runtime.GetScene();
    // Folga para os agentes que nascerem no jogo; passou disso, a multidão dobra (GrowCrowd).
    m_crowdCapacity = std::max(32, static_cast<int>(m_scene->EntitiesWith<NavMeshAgent>().size()) * 2);
    const NavMeshSurface* surface = FindSurface(*m_scene);
    if (!surface) return;
    // Com o .mnav, só lê; sem ele (ou se não abre), gera na hora, como antes de existir o botão do editor.
    if (!surface->navMesh.IsEmpty() && LoadFromFile(*surface)) return;
    Rebuild();
}

bool NavSystem::LoadFromFile(const NavMeshSurface& surface) {
    m_stats = {};
    const std::string name = surface.navMesh.path.empty() ? surface.navMesh.guid : surface.navMesh.path;
    if (surface.navMesh.file.empty()) {
        log::Warn("Navegação: o navmesh {} não foi encontrado no projeto; gerando um no Play", name);
        return false;
    }
    const auto t0 = std::chrono::steady_clock::now();
    std::string error;
    m_crowd.reset();  // anda sobre o navmesh velho
    m_navMesh = NavMesh::Load(surface.navMesh.file, &error);
    ++m_version;
    if (!m_navMesh) {
        log::Warn("Navegação: {}; gerando um navmesh no Play", error);
        return false;
    }
    // A impressão digital da cena de agora contra a do arquivo: cenário ou parâmetros mudaram depois de gerar.
    const NavGeometry geometry = CollectNavGeometry(*m_scene);
    m_stats.fromFile = true;
    m_stats.triangles = static_cast<int>(geometry.TriangleCount());
    m_stats.polygons = m_navMesh->PolygonCount();
    m_stats.stale = NavMesh::SourceHash(geometry, NavSettingsFrom(surface)) != m_navMesh->Source();
    m_stats.buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (m_stats.stale) {
        log::Warn("Navegação: o navmesh {} está desatualizado (o cenário ou a Superfície de navegação mudaram): "
                  "gere de novo no Inspector", name);
    } else {
        log::Info("Navegação: navmesh {} com {} polígonos", name, m_stats.polygons);
    }
    ReplanAgents();
    return true;
}

void NavSystem::OnStop(SceneRuntime&) {
    m_agents.clear();
    m_crowd.reset();
    m_navMesh.reset();
    ++m_version;
    m_scene = nullptr;
}

bool NavSystem::Rebuild() {
    if (!m_scene) return false;
    m_stats = {};
    m_crowd.reset();
    const NavMeshSurface* surface = FindSurface(*m_scene);
    if (!surface) {
        m_navMesh.reset();
        ++m_version;
        ResetCrowd();
        return false;
    }

    m_navMesh = BuildSceneNavMesh(*m_scene, *surface, &m_stats);
    ++m_version;
    if (!m_navMesh) {
        log::Warn("Navegação: navmesh não gerado: {}", m_stats.error);
        ResetCrowd();
        return false;
    }
    log::Info("Navegação: navmesh com {} polígonos ({} triângulos de entrada) em {:.1f} ms", m_stats.polygons,
              m_stats.triangles, m_stats.buildMs);
    ReplanAgents();
    return true;
}

void NavSystem::ResetCrowd() {
    m_crowd.reset();
    for (auto& [id, agent] : m_agents) agent.crowd = -1;
    if (!m_navMesh) return;
    m_crowd = NavCrowd::Create(*m_navMesh, m_crowdCapacity);
    if (!m_crowd) log::Warn("Navegação: o DetourCrowd recusou o navmesh; os agentes ficam parados");
}

void NavSystem::ReplanAgents() {
    // Navmesh novo, multidão nova: cada agente volta para ela onde está e, se estava andando, refaz o caminho até o
    // mesmo destino.
    ResetCrowd();
    if (!m_crowd) return;
    for (auto& [id, agent] : m_agents) {
        const Entity* e = m_scene->Find(id);
        if (!e || !e->Has<NavMeshAgent>() || !AddToCrowd(id, agent, FeetOf(id, *e->Get<NavMeshAgent>()))) continue;
        if (agent.hasDestination && !agent.arrived) PlanPath(agent, agent.destination);
    }
}

void NavSystem::GrowCrowd() {
    // Cheia: outra com o dobro, com os agentes onde estão (as velocidades recomeçam do zero).
    std::vector<std::pair<EntityId, glm::vec3>> where;
    for (const auto& [id, agent] : m_agents) {
        if (agent.crowd >= 0) where.emplace_back(id, m_crowd->Position(agent.crowd));
    }
    m_crowdCapacity *= 2;
    ResetCrowd();
    if (!m_crowd) return;
    for (const auto& [id, feet] : where) {
        Agent& agent = m_agents[id];
        if (AddToCrowd(id, agent, feet) && agent.hasDestination && !agent.arrived) PlanPath(agent, agent.destination);
    }
}

bool NavSystem::AddToCrowd(EntityId id, Agent& agent, const glm::vec3& feet) {
    const Entity* e = m_scene->Find(id);
    if (!m_crowd || !e || !e->Has<NavMeshAgent>()) return false;
    agent.crowd = m_crowd->Add(feet, CrowdParams(*e->Get<NavMeshAgent>()));
    agent.written = feet;
    return agent.crowd >= 0;
}

glm::vec3 NavSystem::FeetOf(EntityId id, const NavMeshAgent& settings) const {
    return glm::vec3(m_scene->WorldMatrix(id)[3]) - glm::vec3(0.0f, settings.baseOffset, 0.0f);
}

void NavSystem::PrepareAgent(EntityId id, Agent& agent, const NavMeshAgent& settings) {
    if (!m_crowd) return;
    const glm::vec3 feet = FeetOf(id, settings);
    if (agent.crowd >= 0 && glm::distance(feet, agent.written) > kMovedOutside) {
        // Alguém mexeu na entidade: a multidão a põe no lugar novo, e o caminho recomeça de lá.
        m_crowd->Remove(agent.crowd);
        agent.crowd = -1;
    }
    if (agent.crowd < 0) {
        if (!AddToCrowd(id, agent, feet)) {
            GrowCrowd();
            if (agent.crowd < 0 && !AddToCrowd(id, agent, feet)) return;
        }
        if (agent.hasDestination && !agent.arrived) PlanPath(agent, agent.destination);
        return;
    }
    m_crowd->SetParams(agent.crowd, CrowdParams(settings));  // um script pode ter mudado a velocidade
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

void NavSystem::NewDestination(Agent& agent, const glm::vec3& destination) {
    agent.destination = destination;
    agent.hasDestination = true;
    agent.arrived = false;
    agent.closest = std::numeric_limits<float>::max();
    agent.stalled = 0.0f;
}

bool NavSystem::PlanPath(Agent& agent, const glm::vec3& destination) {
    NewDestination(agent, destination);
    return m_crowd && agent.crowd >= 0 && m_crowd->SetDestination(agent.crowd, destination);
}

bool NavSystem::SetDestination(EntityId id, const glm::vec3& destination) {
    const Entity* e = m_scene ? m_scene->Find(id) : nullptr;
    const NavMeshAgent* settings = e ? e->Get<NavMeshAgent>() : nullptr;
    if (!settings) return false;
    Agent& agent = AgentFor(id);
    agent.followingTarget = false;
    PrepareAgent(id, agent, *settings);
    return PlanPath(agent, destination);
}

void NavSystem::Stop(EntityId id) {
    if (Agent* agent = FindAgent(id)) {
        agent->hasDestination = false;
        agent->arrived = false;
        agent->followingTarget = false;
        if (m_crowd && agent->crowd >= 0) m_crowd->Stop(agent->crowd);
    }
}

bool NavSystem::HasPath(EntityId id) const {
    const Agent* agent = FindAgent(id);
    return agent && agent->hasDestination && !agent->arrived && m_crowd && agent->crowd >= 0 &&
           (m_crowd->HasPath(agent->crowd) || m_crowd->PathPending(agent->crowd));
}

bool NavSystem::HasArrived(EntityId id) const {
    const Agent* agent = FindAgent(id);
    return agent && agent->arrived;
}

bool NavSystem::IsPathPartial(EntityId id) const {
    const Agent* agent = FindAgent(id);
    return agent && m_crowd && agent->crowd >= 0 && m_crowd->IsPartial(agent->crowd);
}

float NavSystem::RemainingDistance(EntityId id) const {
    return HasPath(id) ? NavMesh::PathLength(m_crowd->Corners(FindAgent(id)->crowd)) : 0.0f;
}

glm::vec3 NavSystem::GetVelocity(EntityId id) const {
    const Agent* agent = FindAgent(id);
    return agent && m_crowd && agent->crowd >= 0 ? m_crowd->Velocity(agent->crowd) : glm::vec3(0.0f);
}

std::vector<glm::vec3> NavSystem::GetPath(EntityId id) const {
    if (!HasPath(id)) return {};
    std::vector<glm::vec3> corners = m_crowd->Corners(FindAgent(id)->crowd);
    if (!corners.empty()) corners.erase(corners.begin());  // o primeiro é onde o agente já está
    return corners;
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
    if (m_crowd && agent.crowd >= 0) m_crowd->Remove(agent.crowd);
    agent = Agent{};
    agent.seen = m_step;
    AddToCrowd(id, agent, *point);
    return true;
}

void NavSystem::OnFixedUpdate(SceneRuntime&, float dt) {
    if (!m_scene) return;
    ++m_step;
    // Cópia: a lista muda se a estrutura da cena mudar.
    const std::vector<EntityId> agents = m_scene->EntitiesWith<NavMeshAgent>();
    if (!agents.empty() && !m_navMesh && !m_warnedNoNavMesh) {
        m_warnedNoNavMesh = true;
        log::Warn("Navegação: há agentes mas não há navmesh (falta uma Superfície de navegação na cena?)");
    }
    // Primeiro todos entram na multidão e recebem o destino; depois ela anda com todos juntos.
    for (EntityId id : agents) {
        if (!m_scene->IsActiveInHierarchy(id)) continue;
        Agent& agent = AgentFor(id);
        const NavMeshAgent& settings = *m_scene->Find(id)->Get<NavMeshAgent>();
        PrepareAgent(id, agent, settings);
        FollowTarget(agent, settings);
    }
    if (m_crowd) m_crowd->Update(dt);
    for (EntityId id : agents) {
        Agent* agent = FindAgent(id);
        if (agent && agent->seen == m_step) ApplyAgent(id, *agent, *m_scene->Find(id)->Get<NavMeshAgent>(), dt);
    }
    // Agentes que sumiram (entidade destruída ou inativa, componente removido) saem da multidão e do registro.
    for (auto it = m_agents.begin(); it != m_agents.end();) {
        if (it->second.seen == m_step) {
            ++it;
            continue;
        }
        if (m_crowd && it->second.crowd >= 0) m_crowd->Remove(it->second.crowd);
        it = m_agents.erase(it);
    }
}

void NavSystem::FollowTarget(Agent& agent, const NavMeshAgent& s) {
    if (s.target.IsEmpty() || !m_crowd || agent.crowd < 0) return;
    if (!m_scene->Find(s.target.id) || !m_scene->IsActiveInHierarchy(s.target.id)) return;
    // Refaz o caminho quando o alvo andou o bastante desde o último (ou quando não há caminho), pela fila.
    const glm::vec3 target = glm::vec3(m_scene->WorldMatrix(s.target.id)[3]);
    const bool lost = !agent.arrived && !m_crowd->HasPath(agent.crowd) && !m_crowd->PathPending(agent.crowd);
    if (agent.followingTarget && glm::distance(target, agent.targetSeen) <= s.repathDistance && !lost) return;
    NewDestination(agent, target);
    agent.followingTarget = true;
    agent.targetSeen = target;
    m_crowd->RequestDestination(agent.crowd, target);
}

void NavSystem::ApplyAgent(EntityId id, Agent& agent, const NavMeshAgent& s, float dt) {
    if (!m_crowd || agent.crowd < 0 || !m_crowd->OnNavMesh(agent.crowd)) return;

    // Chegou: a multidão esquece o destino e freia o agente com a aceleração dele. Vale também parar encostado em
    // quem já parou, sem chegar mais perto (muitos indo ao mesmo ponto): sem isso os de trás empurram para sempre e
    // acabam entrando nos da frente. Encostar em quem ainda anda (engarrafamento no caminho) não conta.
    if (agent.hasDestination && !agent.arrived && m_crowd->HasPath(agent.crowd)) {
        bool known = false;
        const float remaining = m_crowd->RemainingNear(agent.crowd, &known);
        if (remaining < agent.closest - kProgress) {
            agent.closest = remaining;
            agent.stalled = 0.0f;
        } else {
            agent.stalled += dt;
        }
        // Perto do destino (os primeiros a chegar se espremem em volta do ponto), parar de avançar basta; longe, só
        // conta se quem o segura já parou.
        const bool nearGoal = remaining <= s.stoppingDistance + s.radius * 4.0f;
        const bool blocked = agent.stalled >= kBlockedTime && (nearGoal || m_crowd->Touching(agent.crowd, true));
        if (known && (remaining <= std::max(s.stoppingDistance, kArriveReach) || blocked)) {
            agent.arrived = true;
            m_crowd->Stop(agent.crowd);
        }
    }

    // Parado e sem empurrão de ninguém: não mexe no Transform (nem para encostar a altura no navmesh).
    const glm::vec3 feet = m_crowd->Position(agent.crowd);
    const glm::vec3 velocity = m_crowd->Velocity(agent.crowd);
    if (glm::length(velocity) < 1e-4f && glm::length(Flat(feet - agent.written)) < 1e-4f) return;

    // Vira só em Y, no máximo angularSpeed por segundo. A frente das entidades é -Z.
    glm::mat4 world = m_scene->WorldMatrix(id);
    glm::vec3 scale(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
    scale = glm::max(scale, glm::vec3(1e-6f));
    glm::quat rotation = glm::quat_cast(glm::mat3(glm::vec3(world[0]) / scale.x, glm::vec3(world[1]) / scale.y,
                                                  glm::vec3(world[2]) / scale.z));
    const glm::vec3 flatVelocity = Flat(velocity);
    if (s.updateRotation && glm::length(flatVelocity) > 0.01f) {
        const glm::quat facing =
            glm::angleAxis(std::atan2(-flatVelocity.x, -flatVelocity.z), glm::vec3(0.0f, 1.0f, 0.0f));
        const float angle = glm::angle(rotation * glm::inverse(facing));
        const float maxTurn = glm::radians(s.angularSpeed) * dt;
        rotation = angle <= maxTurn || angle < 1e-4f ? facing : glm::slerp(rotation, facing, maxTurn / angle);
    }

    const glm::vec3 lift(0.0f, s.baseOffset, 0.0f);
    world = glm::translate(glm::mat4(1.0f), feet + lift) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
    m_scene->SetWorldMatrix(id, world);
    agent.written = feet;
}

MOTOR_RUNTIME_SYSTEM(Navegacao, NavSystem, kSystemOrderNavigation);

} // namespace motor
