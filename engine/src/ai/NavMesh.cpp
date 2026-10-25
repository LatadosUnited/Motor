#include "motor/ai/NavMesh.h"

#include "ai/NavMeshDetour.h"

#include <DetourNavMeshBuilder.h>
#include <Recast.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace motor {

namespace {

// Todos os polígonos andáveis ganham esta marca; o filtro das consultas aceita qualquer marca.
constexpr unsigned short kWalkableFlag = 1;

// Os objetos da Recast são intermediários da geração: liberados ao sair do Build, dê certo ou não.
struct RecastScratch {
    rcHeightfield* solid = nullptr;
    rcCompactHeightfield* compact = nullptr;
    rcContourSet* contours = nullptr;
    rcPolyMesh* polys = nullptr;
    rcPolyMeshDetail* detail = nullptr;

    ~RecastScratch() {
        rcFreeHeightField(solid);
        rcFreeCompactHeightfield(compact);
        rcFreeContourSet(contours);
        rcFreePolyMesh(polys);
        rcFreePolyMeshDetail(detail);
    }
};

bool Fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

} // namespace

void NavGeometry::AddTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const auto base = static_cast<uint32_t>(vertices.size());
    vertices.insert(vertices.end(), {a, b, c});
    indices.insert(indices.end(), {base, base + 1, base + 2});
}

NavMesh::NavMesh() : m_detour(std::make_unique<Detour>()) {}
NavMesh::~NavMesh() = default;

std::unique_ptr<NavMesh> NavMesh::Build(const NavGeometry& geometry, const NavMeshBuildSettings& s, std::string* error) {
    if (geometry.Empty()) {
        Fail(error, "nenhum triângulo para gerar o navmesh");
        return nullptr;
    }

    // A Recast quer floats soltos e índices int.
    const auto* verts = &geometry.vertices[0].x;
    const int vertCount = static_cast<int>(geometry.vertices.size());
    const std::vector<int> tris(geometry.indices.begin(), geometry.indices.end());
    const int triCount = static_cast<int>(tris.size() / 3);

    rcConfig cfg{};
    cfg.cs = std::max(s.cellSize, 0.01f);
    cfg.ch = std::max(s.cellHeight, 0.01f);
    cfg.walkableSlopeAngle = std::clamp(s.agentMaxSlope, 0.0f, 89.0f);
    cfg.walkableHeight = std::max(3, static_cast<int>(std::ceil(s.agentHeight / cfg.ch)));
    cfg.walkableClimb = std::max(0, static_cast<int>(std::floor(s.agentMaxClimb / cfg.ch)));
    cfg.walkableRadius = std::max(0, static_cast<int>(std::ceil(s.agentRadius / cfg.cs)));
    cfg.maxEdgeLen = static_cast<int>(s.maxEdgeLength / cfg.cs);
    cfg.maxSimplificationError = s.maxEdgeError;
    // A área mínima vem em m²; a Recast conta células.
    cfg.minRegionArea = static_cast<int>(s.minRegionArea / (cfg.cs * cfg.cs));
    cfg.mergeRegionArea = cfg.minRegionArea * 5;
    cfg.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
    cfg.detailSampleDist = s.detailSampleDistance < 0.9f ? 0.0f : cfg.cs * s.detailSampleDistance;
    cfg.detailSampleMaxError = cfg.ch * s.detailMaxError;
    rcCalcBounds(verts, vertCount, cfg.bmin, cfg.bmax);
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);
    if (cfg.width <= 0 || cfg.height <= 0) {
        Fail(error, "a geometria não tem extensão no plano");
        return nullptr;
    }

    rcContext ctx(false);
    RecastScratch scratch;

    // 1. Triângulos -> voxels, com a marca de "andável" nos que não são íngremes demais.
    scratch.solid = rcAllocHeightfield();
    if (!scratch.solid || !rcCreateHeightfield(&ctx, *scratch.solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) {
        Fail(error, "sem memória para a grade de voxels");
        return nullptr;
    }
    std::vector<unsigned char> areas(static_cast<size_t>(triCount), 0);
    rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, verts, vertCount, tris.data(), triCount, areas.data());
    if (!rcRasterizeTriangles(&ctx, verts, vertCount, tris.data(), areas.data(), triCount, *scratch.solid, cfg.walkableClimb)) {
        Fail(error, "falha ao rasterizar os triângulos");
        return nullptr;
    }

    // 2. Tira o que o agente não alcança: obstáculos baixos viram degrau, bordas de queda e vãos baixos saem.
    rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *scratch.solid);
    rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *scratch.solid);
    rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *scratch.solid);

    // 3. Superfícies andáveis, afastadas das paredes pelo raio do agente, divididas em regiões.
    scratch.compact = rcAllocCompactHeightfield();
    if (!scratch.compact ||
        !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *scratch.solid, *scratch.compact)) {
        Fail(error, "falha ao compactar a grade de voxels");
        return nullptr;
    }
    if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *scratch.compact) || !rcBuildDistanceField(&ctx, *scratch.compact) ||
        !rcBuildRegions(&ctx, *scratch.compact, 0, cfg.minRegionArea, cfg.mergeRegionArea)) {
        Fail(error, "falha ao dividir o chão em regiões");
        return nullptr;
    }

    // 4. Contornos das regiões -> polígonos -> detalhe de altura.
    scratch.contours = rcAllocContourSet();
    if (!scratch.contours ||
        !rcBuildContours(&ctx, *scratch.compact, cfg.maxSimplificationError, cfg.maxEdgeLen, *scratch.contours)) {
        Fail(error, "falha ao montar os contornos");
        return nullptr;
    }
    scratch.polys = rcAllocPolyMesh();
    if (!scratch.polys || !rcBuildPolyMesh(&ctx, *scratch.contours, cfg.maxVertsPerPoly, *scratch.polys)) {
        Fail(error, "falha ao montar os polígonos");
        return nullptr;
    }
    if (scratch.polys->npolys == 0) {
        Fail(error, "nenhum lugar onde o agente caiba (chão pequeno, inclinado ou baixo demais?)");
        return nullptr;
    }
    scratch.detail = rcAllocPolyMeshDetail();
    if (!scratch.detail ||
        !rcBuildPolyMeshDetail(&ctx, *scratch.polys, *scratch.compact, cfg.detailSampleDist, cfg.detailSampleMaxError,
                               *scratch.detail)) {
        Fail(error, "falha ao montar o detalhe de altura");
        return nullptr;
    }
    rcPolyMesh& pm = *scratch.polys;
    for (int i = 0; i < pm.npolys; ++i) pm.flags[i] = pm.areas[i] == RC_WALKABLE_AREA ? kWalkableFlag : 0;

    // 5. Polígonos da Recast -> navmesh do Detour (um bloco só; blocos entram com o TileCache, Fase 36).
    if (pm.nverts >= 0xffff) {
        Fail(error, "navmesh grande demais para um bloco só (mais de 65535 vértices)");
        return nullptr;
    }
    // Ligações (off-mesh connections do Detour): mesma marca e área do chão, para as consultas aceitarem.
    const size_t linkCount = geometry.links.size();
    std::vector<float> linkVerts(linkCount * 6), linkRadius(linkCount);
    std::vector<unsigned char> linkDir(linkCount), linkArea(linkCount, RC_WALKABLE_AREA);
    std::vector<unsigned short> linkFlags(linkCount, kWalkableFlag);
    std::vector<unsigned int> linkIds(linkCount);
    for (size_t i = 0; i < linkCount; ++i) {
        const NavLink& link = geometry.links[i];
        rcVcopy(&linkVerts[i * 6], &link.start.x);
        rcVcopy(&linkVerts[i * 6 + 3], &link.end.x);
        linkRadius[i] = std::max(link.radius, 0.01f);
        linkDir[i] = link.bidirectional ? DT_OFFMESH_CON_BIDIR : 0;
        linkIds[i] = static_cast<unsigned int>(i);
    }

    dtNavMeshCreateParams params{};
    params.offMeshConVerts = linkVerts.data();
    params.offMeshConRad = linkRadius.data();
    params.offMeshConDir = linkDir.data();
    params.offMeshConAreas = linkArea.data();
    params.offMeshConFlags = linkFlags.data();
    params.offMeshConUserID = linkIds.data();
    params.offMeshConCount = static_cast<int>(linkCount);
    params.verts = pm.verts;
    params.vertCount = pm.nverts;
    params.polys = pm.polys;
    params.polyAreas = pm.areas;
    params.polyFlags = pm.flags;
    params.polyCount = pm.npolys;
    params.nvp = pm.nvp;
    params.detailMeshes = scratch.detail->meshes;
    params.detailVerts = scratch.detail->verts;
    params.detailVertsCount = scratch.detail->nverts;
    params.detailTris = scratch.detail->tris;
    params.detailTriCount = scratch.detail->ntris;
    params.walkableHeight = s.agentHeight;
    params.walkableRadius = s.agentRadius;
    params.walkableClimb = s.agentMaxClimb;
    rcVcopy(params.bmin, pm.bmin);
    rcVcopy(params.bmax, pm.bmax);
    params.cs = cfg.cs;
    params.ch = cfg.ch;
    params.buildBvTree = true;

    unsigned char* data = nullptr;
    int dataSize = 0;
    if (!dtCreateNavMeshData(&params, &data, &dataSize)) {
        Fail(error, "falha ao montar os dados do navmesh");
        return nullptr;
    }

    std::unique_ptr<NavMesh> nav(new NavMesh());
    nav->m_settings = s;
    nav->m_source = SourceHash(geometry, s);
    Detour& d = *nav->m_detour;
    d.mesh = dtAllocNavMesh();
    if (!d.mesh || dtStatusFailed(d.mesh->init(data, dataSize, DT_TILE_FREE_DATA))) {
        dtFree(data);  // sem o navmesh, ninguém mais libera
        Fail(error, "falha ao iniciar o navmesh");
        return nullptr;
    }
    if (!d.InitQuery()) {
        Fail(error, "falha ao iniciar as consultas do navmesh");
        return nullptr;
    }
    return nav;
}

std::optional<glm::vec3> NavMesh::SamplePosition(const glm::vec3& position, const glm::vec3& extents) const {
    glm::vec3 nearest;
    if (!m_detour->Nearest(position, extents, &nearest.x)) return std::nullopt;
    return nearest;
}

bool NavMesh::FindPath(const glm::vec3& start, const glm::vec3& end, std::vector<glm::vec3>& corners, bool* partial) const {
    corners.clear();
    if (partial) *partial = false;
    const Detour& d = *m_detour;
    const glm::vec3 extents(2.0f, 4.0f, 2.0f);
    glm::vec3 from, to;
    const dtPolyRef startRef = d.Nearest(start, extents, &from.x);
    const dtPolyRef endRef = d.Nearest(end, extents, &to.x);
    if (!startRef || !endRef) return false;

    dtPolyRef polys[kNavMaxPathPolys];
    int polyCount = 0;
    const dtStatus status = d.query->findPath(startRef, endRef, &from.x, &to.x, &d.filter, polys, &polyCount, kNavMaxPathPolys);
    if (dtStatusFailed(status) || polyCount == 0) return false;

    // Fim fora de alcance: o Detour para no polígono mais perto dele; o caminho termina no ponto mais perto lá.
    if (polys[polyCount - 1] != endRef) {
        if (partial) *partial = true;
        d.query->closestPointOnPoly(polys[polyCount - 1], &to.x, &to.x, nullptr);
    } else if (partial && dtStatusDetail(status, DT_PARTIAL_RESULT)) {
        *partial = true;
    }

    float straight[kNavMaxPathPolys * 3];
    int straightCount = 0;
    if (dtStatusFailed(d.query->findStraightPath(&from.x, &to.x, polys, polyCount, straight, nullptr, nullptr,
                                                 &straightCount, kNavMaxPathPolys))) {
        return false;
    }
    corners.reserve(static_cast<size_t>(straightCount));
    for (int i = 0; i < straightCount; ++i) corners.push_back(FromDetour(&straight[i * 3]));
    return !corners.empty();
}

std::optional<NavRaycastHit> NavMesh::Raycast(const glm::vec3& start, const glm::vec3& end) const {
    const Detour& d = *m_detour;
    glm::vec3 from;
    const dtPolyRef startRef = d.Nearest(start, glm::vec3(2.0f, 4.0f, 2.0f), &from.x);
    if (!startRef) return std::nullopt;

    float t = 0.0f;
    glm::vec3 normal(0.0f);
    dtPolyRef visited[kNavMaxPathPolys];
    int visitedCount = 0;
    if (dtStatusFailed(d.query->raycast(startRef, &from.x, &end.x, &d.filter, &t, &normal.x, visited, &visitedCount,
                                        kNavMaxPathPolys))) {
        return std::nullopt;
    }
    if (t >= 1.0f || t == FLT_MAX) return std::nullopt;  // chegou ao fim sem sair do navmesh
    NavRaycastHit hit;
    hit.fraction = t;
    hit.point = from + (end - from) * t;
    hit.normal = normal;
    // O raio do Detour anda no plano: a altura do ponto vem do polígono onde ele parou.
    if (visitedCount > 0) {
        float height = hit.point.y;
        if (dtStatusSucceed(d.query->getPolyHeight(visited[visitedCount - 1], &hit.point.x, &height))) hit.point.y = height;
    }
    return hit;
}

float NavMesh::PathLength(const std::vector<glm::vec3>& corners) {
    float length = 0.0f;
    for (size_t i = 1; i < corners.size(); ++i) length += glm::distance(corners[i - 1], corners[i]);
    return length;
}

int NavMesh::PolygonCount() const {
    const dtNavMesh& mesh = *m_detour->mesh;
    int count = 0;
    for (int i = 0; i < mesh.getMaxTiles(); ++i) {
        const dtMeshTile* tile = mesh.getTile(i);
        if (tile && tile->header) count += tile->header->polyCount;
    }
    return count;
}

std::vector<NavLink> NavMesh::Links() const {
    std::vector<NavLink> out;
    const dtNavMesh& mesh = *m_detour->mesh;
    for (int i = 0; i < mesh.getMaxTiles(); ++i) {
        const dtMeshTile* tile = mesh.getTile(i);
        if (!tile || !tile->header) continue;
        for (int c = 0; c < tile->header->offMeshConCount; ++c) {
            const dtOffMeshConnection& con = tile->offMeshCons[c];
            // Uma das pontas longe de todo polígono: o Detour guarda a ligação, mas ninguém consegue usá-la (a ligação
            // ligada tem duas conexões, uma em cada ponta).
            int connected = 0;
            for (unsigned int l = tile->polys[con.poly].firstLink; l != DT_NULL_LINK; l = tile->links[l].next) ++connected;
            if (connected < 2) continue;
            NavLink link;
            link.start = FromDetour(&con.pos[0]);
            link.end = FromDetour(&con.pos[3]);
            link.radius = con.rad;
            link.bidirectional = (con.flags & DT_OFFMESH_CON_BIDIR) != 0;
            out.push_back(link);
        }
    }
    return out;
}

NavGeometry NavMesh::DebugTriangles() const {
    NavGeometry out;
    const dtNavMesh& mesh = *m_detour->mesh;
    for (int i = 0; i < mesh.getMaxTiles(); ++i) {
        const dtMeshTile* tile = mesh.getTile(i);
        if (!tile || !tile->header) continue;
        for (int p = 0; p < tile->header->polyCount; ++p) {
            const dtPoly& poly = tile->polys[p];
            if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;
            const dtPolyDetail& detail = tile->detailMeshes[p];
            // Índices do detalhe: os primeiros são os vértices do polígono; os outros, do detalhe de altura.
            auto vertex = [&](unsigned char index) {
                if (index < poly.vertCount) return FromDetour(&tile->verts[poly.verts[index] * 3]);
                return FromDetour(&tile->detailVerts[(detail.vertBase + index - poly.vertCount) * 3]);
            };
            for (unsigned int t = 0; t < detail.triCount; ++t) {
                const unsigned char* tri = &tile->detailTris[(detail.triBase + t) * 4];
                out.AddTriangle(vertex(tri[0]), vertex(tri[1]), vertex(tri[2]));
            }
        }
    }
    return out;
}

} // namespace motor
