#pragma once

// Navmesh e consultas do Detour de um NavMesh (só dentro da camada ai): usado por NavMesh.cpp (gerar e consultar) e
// NavMeshFile.cpp (gravar e ler o .mnav).

#include "motor/ai/NavMesh.h"

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

namespace motor {

inline constexpr int kNavMaxQueryNodes = 4096;
inline constexpr int kNavMaxPathPolys = 512;  // polígonos de um caminho
// Área do chão comum na Recast e no Detour (RC_WALKABLE_AREA); as outras áreas usam o próprio número (1 a 15).
inline constexpr unsigned char kNavGroundArea = 63;

inline unsigned char ToDetourArea(int area) { return area <= 0 ? kNavGroundArea : static_cast<unsigned char>(area); }
inline int FromDetourArea(unsigned char area) { return area == kNavGroundArea ? 0 : area; }
// Marca do polígono: um bit por área (a máscara dos agentes é o filtro de marcas do Detour).
inline unsigned short AreaFlag(unsigned char detourArea) {
    return static_cast<unsigned short>(1u << FromDetourArea(detourArea));
}

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

    // Polígono mais perto de `p` (e o ponto nele), pelo filtro dado (nulo: o do navmesh, todas as áreas). 0 se não
    // há navmesh na caixa.
    dtPolyRef Nearest(const glm::vec3& p, const glm::vec3& extents, float* out, const dtQueryFilter* f = nullptr) const {
        dtPolyRef ref = 0;
        if (dtStatusFailed(query->findNearestPoly(&p.x, &extents.x, f ? f : &filter, &ref, out))) return 0;
        return ref;
    }
};

} // namespace motor
