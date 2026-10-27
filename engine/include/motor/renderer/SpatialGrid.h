#pragma once

// Grade de células no chão (Fase 28): acha os objetos de uma região sem testar todos. Serve ao recorte da câmera e
// das sombras, ao raycast do editor e às consultas de área; cada célula é também a unidade do streaming do mundo.
// Só CPU, sem OpenGL: testada no ctest (test_spatial_grid).

#include "motor/renderer/ShadowMath.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace motor {

// Coordenada de uma célula: x e z do mundo divididos pelo tamanho da célula, arredondados para baixo.
struct GridCell {
    int x = 0;
    int z = 0;
    bool operator==(const GridCell&) const = default;
};

// A grade só divide o plano XZ: mundo aberto se espalha no chão, e a altura de cada célula é a dos objetos dela.
// Um objeto entra em todas as células que a caixa dele toca; os que tocariam mais de kMaxCellsPerObject ficam numa
// lista à parte, testada inteira (terreno, céu, chão gigante).
// As consultas usam uma marca interna para não repetir objetos: não consulte a mesma grade de duas threads ao mesmo
// tempo.
class SpatialGrid {
public:
    static constexpr uint32_t kInvalid = UINT32_MAX;
    static constexpr int kMaxCellsPerObject = 64;

    explicit SpatialGrid(float cellSize = 32.0f);

    float CellSize() const { return m_cellSize; }
    GridCell CellOf(const glm::vec3& point) const;
    Aabb CellBounds(GridCell cell, float minY, float maxY) const;

    // Devolve o identificador do objeto (reaproveitado depois do Remove).
    uint32_t Insert(const Aabb& box);
    void Update(uint32_t id, const Aabb& box);
    void Remove(uint32_t id);
    void Clear();

    size_t Count() const { return m_count; }
    size_t CellCount() const { return m_cells.size(); }
    bool Contains(uint32_t id) const { return id < m_objects.size() && m_objects[id].alive; }
    const Aabb& Bounds(uint32_t id) const { return m_objects[id].box; }

    // Os objetos cuja caixa toca o frustum (o mesmo teste do Frustum::Intersects), sem repetir, acrescentados em
    // `out` sem ordem definida.
    void QueryFrustum(const Frustum& frustum, std::vector<uint32_t>& out) const;
    // Os objetos cuja caixa toca `box`.
    void QueryBox(const Aabb& box, std::vector<uint32_t>& out) const;
    // Os objetos cuja caixa o raio cruza até `maxDistance`, do mais perto ao mais longe (pela entrada na caixa).
    // `direction` não precisa ser unitária; a distância é medida em comprimentos dela.
    void QueryRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                  std::vector<uint32_t>& out) const;

private:
    struct Cell {
        std::vector<uint32_t> objects;
        float minY = 0.0f;
        float maxY = 0.0f;
    };
    struct Object {
        Aabb box;
        GridCell first;  // células cobertas: de first a last (inclusive)
        GridCell last;
        bool large = false;
        bool alive = false;
    };

    static uint64_t Key(int x, int z);
    static GridCell FromKey(uint64_t key);
    void Link(uint32_t id);
    void Unlink(uint32_t id);
    bool Mark(uint32_t id) const;  // true na primeira vez que o objeto aparece nesta consulta
    void BeginQuery() const;
    // As células que existem de first a last (inclusive), por coordenada ou pelo mapa, o que for menor.
    void VisitCells(GridCell first, GridCell last, const std::function<void(GridCell, const Cell&)>& visit) const;

    float m_cellSize;
    std::unordered_map<uint64_t, Cell> m_cells;
    std::vector<Object> m_objects;
    std::vector<uint32_t> m_free;
    std::vector<uint32_t> m_large;
    size_t m_count = 0;
    mutable std::vector<uint32_t> m_stamps;  // por objeto: a última consulta em que apareceu
    mutable uint32_t m_query = 0;
};

} // namespace motor
