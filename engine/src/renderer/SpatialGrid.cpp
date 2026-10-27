#include "motor/renderer/SpatialGrid.h"

#include <glm/geometric.hpp>

#include <glm/common.hpp>

#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace motor {

namespace {

int CellCoord(float value, float cellSize) {
    // Coordenadas absurdas (NaN, infinito) caem nas pontas em vez de estourar o int.
    const float cell = std::floor(value / cellSize);
    if (!(cell > -1e9f)) return -1000000000;
    if (cell > 1e9f) return 1000000000;
    return static_cast<int>(cell);
}

// A caixa inteira do lado de dentro dos seis planos: os objetos da célula não precisam do teste um a um.
bool FrustumContains(const Frustum& frustum, const Aabb& box) {
    for (const glm::vec4& p : frustum.planes) {
        // Canto da caixa mais "para fora" do plano.
        const glm::vec3 v{p.x >= 0.0f ? box.min.x : box.max.x, p.y >= 0.0f ? box.min.y : box.max.y,
                          p.z >= 0.0f ? box.min.z : box.max.z};
        if (glm::dot(glm::vec3(p), v) + p.w < 0.0f) return false;
    }
    return true;
}

// Caixa dos 8 cantos do frustum, cada um o encontro de três planos (esquerda/direita, baixo/cima, perto/longe).
// Sem resposta se dois planos forem paralelos.
std::optional<Aabb> FrustumBox(const Frustum& frustum) {
    Aabb box{glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
    const auto& p = frustum.planes;
    for (int i = 0; i < 8; ++i) {
        const glm::vec4& a = p[i & 1 ? 1 : 0];
        const glm::vec4& b = p[i & 2 ? 3 : 2];
        const glm::vec4& c = p[i & 4 ? 5 : 4];
        const glm::vec3 na(a), nb(b), nc(c);
        const glm::vec3 bc = glm::cross(nb, nc);
        const float det = glm::dot(na, bc);
        if (std::abs(det) < 1e-12f) return std::nullopt;
        const glm::vec3 corner = (-a.w * bc - b.w * glm::cross(nc, na) - c.w * glm::cross(na, nb)) / det;
        if (!std::isfinite(corner.x) || !std::isfinite(corner.z)) return std::nullopt;
        box.min = glm::min(box.min, corner);
        box.max = glm::max(box.max, corner);
    }
    return box;
}

bool BoxesTouch(const Aabb& a, const Aabb& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

// Entrada do raio na caixa (método das placas), ou -1 se não cruza até `maxDistance`. Origem dentro dá 0.
float RayEnter(const glm::vec3& origin, const glm::vec3& invDir, float maxDistance, const Aabb& box) {
    float enter = 0.0f, leave = maxDistance;
    for (int axis = 0; axis < 3; ++axis) {
        float t0 = (box.min[axis] - origin[axis]) * invDir[axis];
        float t1 = (box.max[axis] - origin[axis]) * invDir[axis];
        if (t0 > t1) std::swap(t0, t1);
        // Direção 0 no eixo dá ±infinito (dentro da placa) ou NaN (na borda dela): NaN conta como dentro.
        if (!(t0 <= enter)) enter = std::isnan(t0) ? enter : t0;
        if (!(t1 >= leave)) leave = std::isnan(t1) ? leave : t1;
        if (enter > leave) return -1.0f;
    }
    return enter;
}

} // namespace

SpatialGrid::SpatialGrid(float cellSize) : m_cellSize(cellSize > 0.0f ? cellSize : 32.0f) {}

GridCell SpatialGrid::CellOf(const glm::vec3& point) const {
    return {CellCoord(point.x, m_cellSize), CellCoord(point.z, m_cellSize)};
}

Aabb SpatialGrid::CellBounds(GridCell cell, float minY, float maxY) const {
    return {{static_cast<float>(cell.x) * m_cellSize, minY, static_cast<float>(cell.z) * m_cellSize},
            {static_cast<float>(cell.x + 1) * m_cellSize, maxY, static_cast<float>(cell.z + 1) * m_cellSize}};
}

uint64_t SpatialGrid::Key(int x, int z) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(z);
}

GridCell SpatialGrid::FromKey(uint64_t key) {
    return {static_cast<int>(static_cast<uint32_t>(key >> 32)), static_cast<int>(static_cast<uint32_t>(key))};
}

uint32_t SpatialGrid::Insert(const Aabb& box) {
    uint32_t id;
    if (!m_free.empty()) {
        id = m_free.back();
        m_free.pop_back();
    } else {
        id = static_cast<uint32_t>(m_objects.size());
        m_objects.emplace_back();
        m_stamps.push_back(0);
    }
    m_objects[id].box = box;
    m_objects[id].alive = true;
    Link(id);
    ++m_count;
    return id;
}

void SpatialGrid::Update(uint32_t id, const Aabb& box) {
    if (!Contains(id)) return;
    Object& object = m_objects[id];
    const GridCell first = CellOf(box.min), last = CellOf(box.max);
    if (!object.large && first == object.first && last == object.last) {
        // Mesmas células (o caso comum de quem se mexe pouco): só a caixa e a altura das células.
        object.box = box;
        for (int x = first.x; x <= last.x; ++x) {
            for (int z = first.z; z <= last.z; ++z) {
                Cell& cell = m_cells[Key(x, z)];
                cell.minY = std::min(cell.minY, box.min.y);
                cell.maxY = std::max(cell.maxY, box.max.y);
            }
        }
        return;
    }
    Unlink(id);
    object.box = box;
    Link(id);
}

void SpatialGrid::Remove(uint32_t id) {
    if (!Contains(id)) return;
    Unlink(id);
    m_objects[id].alive = false;
    m_free.push_back(id);
    --m_count;
}

void SpatialGrid::Clear() {
    m_cells.clear();
    m_objects.clear();
    m_free.clear();
    m_large.clear();
    m_stamps.clear();
    m_count = 0;
}

void SpatialGrid::Link(uint32_t id) {
    Object& object = m_objects[id];
    object.first = CellOf(object.box.min);
    object.last = CellOf(object.box.max);
    const int64_t cells = (static_cast<int64_t>(object.last.x) - object.first.x + 1) *
                          (static_cast<int64_t>(object.last.z) - object.first.z + 1);
    object.large = cells > kMaxCellsPerObject;
    if (object.large) {
        m_large.push_back(id);
        return;
    }
    for (int x = object.first.x; x <= object.last.x; ++x) {
        for (int z = object.first.z; z <= object.last.z; ++z) {
            auto [it, created] = m_cells.try_emplace(Key(x, z));
            Cell& cell = it->second;
            if (created) {
                cell.minY = object.box.min.y;
                cell.maxY = object.box.max.y;
            } else {
                cell.minY = std::min(cell.minY, object.box.min.y);
                cell.maxY = std::max(cell.maxY, object.box.max.y);
            }
            cell.objects.push_back(id);
        }
    }
}

void SpatialGrid::Unlink(uint32_t id) {
    const Object& object = m_objects[id];
    if (object.large) {
        auto it = std::find(m_large.begin(), m_large.end(), id);
        if (it != m_large.end()) {
            *it = m_large.back();
            m_large.pop_back();
        }
        return;
    }
    for (int x = object.first.x; x <= object.last.x; ++x) {
        for (int z = object.first.z; z <= object.last.z; ++z) {
            auto found = m_cells.find(Key(x, z));
            if (found == m_cells.end()) continue;
            Cell& cell = found->second;
            auto it = std::find(cell.objects.begin(), cell.objects.end(), id);
            if (it != cell.objects.end()) {
                *it = cell.objects.back();
                cell.objects.pop_back();
            }
            if (cell.objects.empty()) {
                m_cells.erase(found);
                continue;
            }
            // A altura da célula só encolhe se o objeto que saiu era o mais baixo ou o mais alto dela.
            if (object.box.min.y <= cell.minY || object.box.max.y >= cell.maxY) {
                cell.minY = std::numeric_limits<float>::max();
                cell.maxY = std::numeric_limits<float>::lowest();
                for (uint32_t other : cell.objects) {
                    cell.minY = std::min(cell.minY, m_objects[other].box.min.y);
                    cell.maxY = std::max(cell.maxY, m_objects[other].box.max.y);
                }
            }
        }
    }
}

void SpatialGrid::BeginQuery() const {
    if (++m_query == 0) {
        // Deu a volta no contador: zera as marcas para nenhuma antiga parecer desta consulta.
        std::fill(m_stamps.begin(), m_stamps.end(), 0u);
        m_query = 1;
    }
}

bool SpatialGrid::Mark(uint32_t id) const {
    if (m_stamps[id] == m_query) return false;
    m_stamps[id] = m_query;
    return true;
}

void SpatialGrid::QueryFrustum(const Frustum& frustum, std::vector<uint32_t>& out) const {
    BeginQuery();
    // Só as células debaixo da caixa do frustum (os 8 cantos): percorrer todas as células custava mais que testar os
    // objetos quando a câmera vê pouco de um mundo grande.
    GridCell first{INT_MIN, INT_MIN}, last{INT_MAX, INT_MAX};
    if (const std::optional<Aabb> box = FrustumBox(frustum)) {
        first = CellOf(box->min);
        last = CellOf(box->max);
    }
    auto visit = [&](GridCell coord, const Cell& cell) {
        const Aabb bounds = CellBounds(coord, cell.minY, cell.maxY);
        if (!frustum.Intersects(bounds)) return;
        // Célula inteira dentro: os objetos dela (que podem passar das bordas) só estão dentro se a caixa deles
        // também estiver na célula; os que passam são testados.
        const bool inside = FrustumContains(frustum, bounds);
        for (uint32_t id : cell.objects) {
            if (m_stamps[id] == m_query) continue;
            const Object& object = m_objects[id];
            const bool within = inside && object.first == object.last;
            if (within || frustum.Intersects(object.box)) {
                m_stamps[id] = m_query;
                out.push_back(id);
            }
        }
    };
    VisitCells(first, last, visit);
    for (uint32_t id : m_large) {
        if (frustum.Intersects(m_objects[id].box)) out.push_back(id);
    }
}

void SpatialGrid::VisitCells(GridCell first, GridCell last,
                             const std::function<void(GridCell, const Cell&)>& visit) const {
    const int64_t span = (static_cast<int64_t>(last.x) - first.x + 1) * (static_cast<int64_t>(last.z) - first.z + 1);
    if (span > static_cast<int64_t>(m_cells.size())) {
        // Região maior que a parte ocupada da grade: mais barato passar pelas células que existem.
        for (const auto& [key, cell] : m_cells) {
            const GridCell coord = FromKey(key);
            if (coord.x >= first.x && coord.x <= last.x && coord.z >= first.z && coord.z <= last.z) visit(coord, cell);
        }
        return;
    }
    for (int x = first.x; x <= last.x; ++x) {
        for (int z = first.z; z <= last.z; ++z) {
            auto it = m_cells.find(Key(x, z));
            if (it != m_cells.end()) visit({x, z}, it->second);
        }
    }
}

void SpatialGrid::QueryBox(const Aabb& box, std::vector<uint32_t>& out) const {
    BeginQuery();
    VisitCells(CellOf(box.min), CellOf(box.max), [&](GridCell, const Cell& cell) {
        for (uint32_t id : cell.objects) {
            if (m_stamps[id] != m_query && BoxesTouch(m_objects[id].box, box) && Mark(id)) out.push_back(id);
        }
    });
    for (uint32_t id : m_large) {
        if (BoxesTouch(m_objects[id].box, box)) out.push_back(id);
    }
}

void SpatialGrid::QueryRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                           std::vector<uint32_t>& out) const {
    BeginQuery();
    const glm::vec3 invDir = 1.0f / direction;  // eixo parado vira infinito, tratado no RayEnter
    std::vector<std::pair<float, uint32_t>> hits;
    for (const auto& [key, cell] : m_cells) {
        const GridCell coord = FromKey(key);
        if (RayEnter(origin, invDir, maxDistance, CellBounds(coord, cell.minY, cell.maxY)) < 0.0f) continue;
        for (uint32_t id : cell.objects) {
            if (m_stamps[id] == m_query) continue;
            const float t = RayEnter(origin, invDir, maxDistance, m_objects[id].box);
            if (t >= 0.0f && Mark(id)) hits.emplace_back(t, id);
        }
    }
    for (uint32_t id : m_large) {
        const float t = RayEnter(origin, invDir, maxDistance, m_objects[id].box);
        if (t >= 0.0f) hits.emplace_back(t, id);
    }
    std::sort(hits.begin(), hits.end());
    for (const auto& hit : hits) out.push_back(hit.second);
}

} // namespace motor
