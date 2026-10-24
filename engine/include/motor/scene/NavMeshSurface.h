#pragma once

// Superfície de navegação (Fase 34): liga o navmesh da cena e diz o tamanho do agente para o qual ele é gerado.
// Só dados: quem gera é o NavSystem (motor/ai/NavSystem.h), no começo do Play, a partir dos Colliders estáticos
// (sem Rigidbody e sem trigger) das entidades ativas. Uma por cena; se houver mais, vale a primeira.

#include "motor/scene/Component.h"

namespace motor {

struct NavMeshSurface {
    float agentRadius = 0.5f;     // distância que o navmesh mantém das paredes
    float agentHeight = 2.0f;     // vão livre mínimo
    float agentMaxClimb = 0.4f;   // degrau mais alto
    float agentMaxSlope = 45.0f;  // graus
    float cellSize = 0.25f;       // lado do voxel: menor = mais fiel e mais lento
    float cellHeight = 0.125f;
    float minRegionArea = 4.0f;   // m²: ilhas menores somem

    bool operator==(const NavMeshSurface&) const = default;
};

} // namespace motor
