#pragma once

// Objetos guardados entre quadros (Fase 28). Quem tem muitos objetos que não mudam (cenário, terreno, vegetação)
// registra cada um uma vez aqui, e o Renderer::SubmitWorld os põe no quadro sem refazer o que já se sabe deles:
// caixa no mundo, matriz da normal e número do material. Os objetos ficam numa grade de células (SpatialGrid): o
// renderer só põe no quadro os que a câmera ou alguma passada de sombra vê, sem olhar os outros.
// Só CPU, sem OpenGL: testado no ctest (test_render_world).
// Cada cena tem o seu (o editor desenha mais de uma cena com o mesmo renderer).

#include "motor/renderer/DrawOrder.h"
#include "motor/renderer/Material.h"
#include "motor/renderer/Mesh.h"
#include "motor/renderer/ShadowMath.h"
#include "motor/renderer/SpatialGrid.h"

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace motor {

// Um objeto: malha (sem pele) e material ficam vivos enquanto o objeto estiver no mundo. Material de outro dono (o
// próprio de um componente) pode vir num shared_ptr sem dono (construtor de alias), se quem chama garante a vida.
// Mudar os valores do material depois de registrar não aparece: chame Update.
struct RenderObject {
    std::shared_ptr<const Mesh> mesh;
    std::shared_ptr<const Material> material;
    glm::mat4 model{1.0f};
    ShadowCasting shadows = ShadowCasting::On;
};

class RenderWorld {
public:
    using Id = uint32_t;
    static constexpr Id kInvalid = UINT32_MAX;

    // kInvalid se faltar malha ou material, ou se a malha tiver pele (essas vão pelo SubmitSkinned a cada quadro).
    Id Add(const RenderObject& object);
    bool Update(Id id, const RenderObject& object);
    bool SetTransform(Id id, const glm::mat4& model);  // só mover: material e malha ficam
    void Remove(Id id);
    void Clear();

    size_t Count() const { return m_entries.size(); }
    bool Contains(Id id) const { return id < m_indexOf.size() && m_indexOf[id] != kInvalid; }
    const Aabb& Bounds(Id id) const { return m_entries[m_indexOf[id]].bounds; }
    // Materiais diferentes (pelos valores, não pelo objeto) entre os registrados.
    size_t MaterialCount() const { return m_materials.Size(); }
    // Ids dos objetos cuja caixa toca o frustum, pela grade, sem ordem definida. Uma thread de cada vez.
    void Query(const Frustum& frustum, std::vector<Id>& out) const;

private:
    friend class Renderer;

    struct Entry {
        std::shared_ptr<const Mesh> mesh;
        std::shared_ptr<const Material> material;
        Aabb bounds;              // no mundo, da malha base
        uint32_t materialId = 0;  // em m_materials e m_shadows (deste mundo; o renderer troca pelos do quadro)
        uint32_t shadowId = 0;
        ShadowCasting shadows = ShadowCasting::On;
        bool transparent = false;
        uint32_t cell = SpatialGrid::kInvalid;  // na m_grid
    };
    void Fill(Entry& entry, const RenderObject& object);
    void SetMatrices(size_t index, const glm::mat4& model);
    void QueryEntries(const Frustum& frustum, std::vector<uint32_t>& out) const;  // posições em m_entries

    std::vector<Entry> m_entries;      // sem buracos: Remove traz o último para o lugar
    std::vector<glm::mat4> m_objects;  // 2 por entrada (mundo e normal), no formato do buffer de objetos
    std::vector<uint32_t> m_indexOf;   // id -> entrada (kInvalid = removido)
    std::vector<Id> m_idOf;            // entrada -> id
    std::vector<Id> m_free;
    SpatialGrid m_grid;
    std::vector<uint32_t> m_entryOfCell;  // id na grade -> entrada
    mutable std::vector<uint32_t> m_hits;  // da última consulta à grade
    MaterialIds m_materials;  // só crescem até o Clear (Update que troca de material deixa a chave antiga)
    ShadowParamsIds m_shadows;
};

} // namespace motor
