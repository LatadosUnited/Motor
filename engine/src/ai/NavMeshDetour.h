#pragma once

// Navmesh e consultas do Detour de um NavMesh (só dentro da camada ai): usado por NavMesh.cpp (gerar e consultar) e
// NavMeshFile.cpp (gravar e ler o .mnav).

#include "motor/ai/NavMesh.h"

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

namespace motor {

inline constexpr int kNavMaxQueryNodes = 4096;

inline glm::vec3 FromDetour(const float* v) { return {v[0], v[1], v[2]}; }

struct NavMesh::Detour {
    dtNavMesh* mesh = nullptr;
    dtNavMeshQuery* query = nullptr;
    dtQueryFilter filter;

    ~Detour() {
        dtFreeNavMeshQuery(query);
        dtFreeNavMesh(mesh);
    }

    // Consultas e filtro sobre o `mesh` já iniciado. false se o Detour recusar.
    bool InitQuery() {
        query = dtAllocNavMeshQuery();
        if (!query || dtStatusFailed(query->init(mesh, kNavMaxQueryNodes))) return false;
        filter.setIncludeFlags(0xffff);
        filter.setExcludeFlags(0);
        return true;
    }

    // Polígono mais perto de `p` (e o ponto nele). 0 se não há navmesh na caixa.
    dtPolyRef Nearest(const glm::vec3& p, const glm::vec3& extents, float* out) const {
        dtPolyRef ref = 0;
        if (dtStatusFailed(query->findNearestPoly(&p.x, &extents.x, &filter, &ref, out))) return 0;
        return ref;
    }
};

} // namespace motor
