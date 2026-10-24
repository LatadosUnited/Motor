#include "NavCrowd.h"

#include "NavMeshDetour.h"

#include <DetourCommon.h>
#include <DetourCrowd.h>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace motor {

namespace {

// Qualidade do desvio (as quatro do exemplo da Recast); os agentes usam a "boa".
constexpr unsigned char kAvoidanceGood = 2;

dtCrowdAgentParams ToDetour(const NavCrowd::AgentParams& p) {
    dtCrowdAgentParams out{};
    out.radius = std::clamp(p.radius, 0.01f, NavCrowd::kMaxAgentRadius);
    out.height = std::max(p.height, 0.01f);
    out.maxAcceleration = std::max(p.acceleration, 0.0f);
    out.maxSpeed = std::max(p.speed, 0.0f);
    // Alcances do exemplo da Recast: vizinhos até 12 raios, atalhos de visão até 30.
    out.collisionQueryRange = out.radius * 12.0f;
    out.pathOptimizationRange = out.radius * 30.0f;
    out.separationWeight = p.avoidOthers ? 2.0f : 0.0f;
    out.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO;
    if (p.avoidOthers) out.updateFlags |= DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_SEPARATION;
    out.obstacleAvoidanceType = kAvoidanceGood;
    out.queryFilterType = 0;
    return out;
}

bool Pending(unsigned char state) {
    return state == DT_CROWDAGENT_TARGET_REQUESTING || state == DT_CROWDAGENT_TARGET_WAITING_FOR_QUEUE ||
           state == DT_CROWDAGENT_TARGET_WAITING_FOR_PATH;
}

} // namespace

std::unique_ptr<NavCrowd> NavCrowd::Create(const NavMesh& navMesh, int capacity) {
    std::unique_ptr<NavCrowd> crowd(new NavCrowd());
    crowd->m_navMesh = &navMesh;
    crowd->m_capacity = std::max(capacity, 1);
    crowd->m_touching.assign(static_cast<size_t>(crowd->m_capacity), 0);
    crowd->m_crowd = dtAllocCrowd();
    if (!crowd->m_crowd || !crowd->m_crowd->init(crowd->m_capacity, kMaxAgentRadius, navMesh.m_detour->mesh)) {
        return nullptr;
    }
    // Mesmo filtro das consultas do NavMesh: qualquer polígono marcado.
    dtQueryFilter* filter = crowd->m_crowd->getEditableFilter(0);
    filter->setIncludeFlags(0xffff);
    filter->setExcludeFlags(0);

    // Níveis de qualidade do desvio, do exemplo da Recast (mais divisões e anéis = escolhe melhor e custa mais).
    dtObstacleAvoidanceParams params = *crowd->m_crowd->getObstacleAvoidanceParams(0);
    const unsigned char levels[4][3] = {{5, 2, 1}, {5, 2, 2}, {7, 2, 3}, {7, 3, 3}};
    for (int i = 0; i < 4; ++i) {
        params.velBias = 0.5f;
        params.adaptiveDivs = levels[i][0];
        params.adaptiveRings = levels[i][1];
        params.adaptiveDepth = levels[i][2];
        crowd->m_crowd->setObstacleAvoidanceParams(i, &params);
    }
    return crowd;
}

NavCrowd::~NavCrowd() { dtFreeCrowd(m_crowd); }

int NavCrowd::Add(const glm::vec3& position, const AgentParams& params) {
    const dtCrowdAgentParams p = ToDetour(params);
    return m_crowd->addAgent(&position.x, &p);
}

void NavCrowd::Remove(int index) { m_crowd->removeAgent(index); }

void NavCrowd::SetParams(int index, const AgentParams& params) {
    const dtCrowdAgentParams p = ToDetour(params);
    m_crowd->updateAgentParameters(index, &p);
}

bool NavCrowd::SetDestination(int index, const glm::vec3& destination, bool* partial) {
    if (partial) *partial = false;
    dtCrowdAgent* ag = m_crowd->getEditableAgent(index);
    if (!ag || !ag->active || ag->state != DT_CROWDAGENT_STATE_WALKING) return false;
    const NavMesh::Detour& d = *m_navMesh->m_detour;
    glm::vec3 to;
    const dtPolyRef endRef = d.Nearest(destination, glm::vec3(2.0f, 4.0f, 2.0f), &to.x);
    const dtPolyRef startRef = ag->corridor.getFirstPoly();
    if (!startRef || !endRef) return false;

    dtPolyRef polys[kNavMaxPathPolys];
    int count = 0;
    const dtStatus status = d.query->findPath(startRef, endRef, ag->npos, &to.x, &d.filter, polys, &count, kNavMaxPathPolys);
    if (dtStatusFailed(status) || count == 0) return false;
    // Fim fora de alcance (outra ilha): o caminho termina no ponto mais perto dele, como no NavMesh::FindPath.
    bool isPartial = dtStatusDetail(status, DT_PARTIAL_RESULT);
    if (polys[count - 1] != endRef) {
        isPartial = true;
        d.query->closestPointOnPoly(polys[count - 1], &to.x, &to.x, nullptr);
    }

    // O que o DetourCrowd faz quando um caminho da fila chega: corredor novo, borda refeita, destino válido.
    ag->corridor.setCorridor(&to.x, polys, count);
    ag->boundary.reset();
    ag->partial = isPartial;
    ag->targetState = DT_CROWDAGENT_TARGET_VALID;
    ag->targetRef = polys[count - 1];
    dtVcopy(ag->targetPos, &to.x);
    ag->targetReplan = false;
    ag->targetReplanTime = 0.0f;
    if (partial) *partial = isPartial;
    return true;
}

bool NavCrowd::RequestDestination(int index, const glm::vec3& destination) {
    const NavMesh::Detour& d = *m_navMesh->m_detour;
    glm::vec3 to;
    const dtPolyRef ref = d.Nearest(destination, glm::vec3(2.0f, 4.0f, 2.0f), &to.x);
    return ref && m_crowd->requestMoveTarget(index, ref, &to.x);
}

void NavCrowd::Stop(int index) { m_crowd->resetMoveTarget(index); }

void NavCrowd::Update(float dt) {
    m_crowd->update(dt, nullptr);
    SeparateOverlaps();
}

bool NavCrowd::OnNavMesh(int index) const {
    const dtCrowdAgent* ag = m_crowd->getAgent(index);
    return ag && ag->active && ag->state != DT_CROWDAGENT_STATE_INVALID;
}

bool NavCrowd::PathPending(int index) const {
    const dtCrowdAgent* ag = m_crowd->getAgent(index);
    return ag && ag->active && Pending(ag->targetState);
}

bool NavCrowd::HasPath(int index) const {
    const dtCrowdAgent* ag = m_crowd->getAgent(index);
    return ag && ag->active && ag->targetState == DT_CROWDAGENT_TARGET_VALID && ag->corridor.getPathCount() > 0;
}

bool NavCrowd::IsPartial(int index) const {
    const dtCrowdAgent* ag = m_crowd->getAgent(index);
    return ag && ag->active && ag->partial;
}

glm::vec3 NavCrowd::Position(int index) const { return FromDetour(m_crowd->getAgent(index)->npos); }

glm::vec3 NavCrowd::Velocity(int index) const { return FromDetour(m_crowd->getAgent(index)->vel); }

float NavCrowd::RemainingNear(int index, bool* known) const {
    const dtCrowdAgent* ag = m_crowd->getAgent(index);
    const glm::vec3 position = FromDetour(ag->npos);
    if (ag->ncorners == 0) {
        // Sem cantos à frente: o destino é o próximo ponto.
        if (known) *known = true;
        return glm::distance(position, FromDetour(ag->corridor.getTarget()));
    }
    float distance = glm::distance(position, FromDetour(&ag->cornerVerts[0]));
    for (int i = 1; i < ag->ncorners; ++i) {
        distance += glm::distance(FromDetour(&ag->cornerVerts[(i - 1) * 3]), FromDetour(&ag->cornerVerts[i * 3]));
    }
    if (known) *known = (ag->cornerFlags[ag->ncorners - 1] & DT_STRAIGHTPATH_END) != 0;
    return distance;
}

bool NavCrowd::Touching(int index, bool idleOnly) const {
    const unsigned char flags = m_touching[static_cast<size_t>(index)];
    return (flags & (idleOnly ? kTouchingIdle : kTouching)) != 0;
}

void NavCrowd::SeparateOverlaps() {
    // A colisão do DetourCrowd olha só os primeiros 32 agentes da grade dela (não os mais perto) e não separa dois
    // parados no mesmo ponto: numa multidão densa, uns acabam dentro dos outros. Aqui, depois do passo dela, cada
    // par que se sobrepõe é afastado pela metade do que entrou cada um, com uma grade própria do tamanho dos agentes.
    const NavMesh::Detour& d = *m_navMesh->m_detour;
    std::vector<int> agents;
    float cell = 0.0f;
    for (int i = 0; i < m_capacity; ++i) {
        const dtCrowdAgent* ag = m_crowd->getAgent(i);
        m_touching[static_cast<size_t>(i)] = 0;
        if (!ag || !ag->active || ag->state != DT_CROWDAGENT_STATE_WALKING) continue;
        agents.push_back(i);
        cell = std::max(cell, ag->params.radius * 2.0f);
    }
    if (agents.size() < 2) return;
    cell *= 1.25f;  // a folga de "encostado" cabe numa célula
    auto cellOf = [&](float v) { return static_cast<int>(std::floor(v / cell)); };
    auto key = [](int x, int z) {
        return static_cast<int64_t>((static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(z));
    };
    std::unordered_map<int64_t, std::vector<int>> grid;
    for (int i : agents) {
        const float* p = m_crowd->getAgent(i)->npos;
        grid[key(cellOf(p[0]), cellOf(p[2]))].push_back(i);
    }

    std::vector<glm::vec2> push(static_cast<size_t>(m_capacity), glm::vec2(0.0f));
    for (int i : agents) {
        const dtCrowdAgent* a = m_crowd->getAgent(i);
        const int cx = cellOf(a->npos[0]);
        const int cz = cellOf(a->npos[2]);
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const auto it = grid.find(key(cx + dx, cz + dz));
                if (it == grid.end()) continue;
                for (int j : it->second) {
                    if (j == i) continue;
                    const dtCrowdAgent* b = m_crowd->getAgent(j);
                    const glm::vec2 diff(a->npos[0] - b->npos[0], a->npos[2] - b->npos[2]);
                    const float dist = glm::length(diff);
                    const float sum = a->params.radius + b->params.radius;
                    if (dist < sum * 1.25f) {
                        m_touching[static_cast<size_t>(i)] |= kTouching;
                        if (b->targetState == DT_CROWDAGENT_TARGET_NONE) m_touching[static_cast<size_t>(i)] |= kTouchingIdle;
                    }
                    if (dist >= sum || j < i) continue;  // cada par uma vez
                    // No mesmo ponto: direção tirada do índice (ângulo de ouro), para os dois saírem para lados opostos.
                    const float angle = static_cast<float>(i) * 2.39996f;
                    const glm::vec2 dir = dist > 1e-4f ? diff / dist : glm::vec2(std::cos(angle), std::sin(angle));
                    const float half = (sum - dist) * 0.5f;
                    push[static_cast<size_t>(i)] += dir * half;
                    push[static_cast<size_t>(j)] -= dir * half;
                }
            }
        }
    }
    for (int i : agents) {
        const glm::vec2 p = push[static_cast<size_t>(i)];
        if (glm::dot(p, p) < 1e-10f) continue;
        dtCrowdAgent* ag = m_crowd->getEditableAgent(i);
        // Pelo corredor: o empurrão não tira o agente do navmesh nem o passa através de uma parede.
        const float target[3] = {ag->npos[0] + p.x, ag->npos[1], ag->npos[2] + p.y};
        ag->corridor.movePosition(target, d.query, &d.filter);
        dtVcopy(ag->npos, ag->corridor.getPos());
    }
}

std::vector<glm::vec3> NavCrowd::Corners(int index) const {
    const dtCrowdAgent* ag = m_crowd->getAgent(index);
    if (!ag || !ag->active || ag->corridor.getPathCount() == 0) return {};
    const NavMesh::Detour& d = *m_navMesh->m_detour;
    float straight[kNavMaxPathPolys * 3];
    int count = 0;
    if (dtStatusFailed(d.query->findStraightPath(ag->npos, ag->corridor.getTarget(), ag->corridor.getPath(),
                                                 ag->corridor.getPathCount(), straight, nullptr, nullptr, &count,
                                                 kNavMaxPathPolys))) {
        return {};
    }
    std::vector<glm::vec3> corners;
    corners.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) corners.push_back(FromDetour(&straight[i * 3]));
    return corners;
}

} // namespace motor
