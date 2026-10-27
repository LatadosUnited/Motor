#pragma once

// Terreno (Fase 28): liga um arquivo .mterrain (mapa de altura em blocos, motor/assets/TerrainData.h) à entidade. A
// cena desenha cada bloco como um objeto estático (Scene::SubmitTo), na posição da entidade, e o Raycast do editor
// acerta os blocos. Pincéis, pintura, física e LOD vêm nas próximas tarefas da fase.

#include "motor/assets/TerrainData.h"
#include "motor/renderer/Material.h"
#include "motor/scene/Component.h"

#include <filesystem>
#include <memory>
#include <string>

namespace motor {

// Arquivo .mterrain do projeto, pelo GUID (com o caminho junto, como plano B), como o NavMeshData.
struct TerrainRef {
    std::string guid;
    std::string path;  // relativo ao projeto ("Assets/Terrenos/Ilha.mterrain")
    // Arquivo no disco, resolvido pela AssetLibrary ao ler a cena. Não vai para a cena nem entra na comparação.
    std::filesystem::path file;

    bool IsEmpty() const { return guid.empty() && path.empty(); }
    bool operator==(const TerrainRef& other) const { return guid == other.guid && path == other.path; }

    // Alturas do arquivo, abertas na primeira chamada (TerrainData::Open: as cópias da cena dividem o mesmo objeto).
    // Nulo sem arquivo ou se não deu para ler.
    TerrainData* Data() const;
    // Troca as alturas em memória (o editor, ao criar um terreno novo, antes de qualquer leitura do disco).
    void SetData(std::shared_ptr<TerrainData> data) const;

private:
    mutable std::shared_ptr<TerrainData> m_data;
    mutable std::filesystem::path m_loaded;  // de onde veio m_data: outro `file` abre de novo
};

struct Terrain {
    TerrainRef terrain;
    std::shared_ptr<Material> sharedMaterial;  // nulo = usa `material`
    Material material = DefaultMaterial();
    ShadowCasting shadows = ShadowCasting::On;

    const Material& Resolved() const { return sharedMaterial ? *sharedMaterial : material; }

    static Material DefaultMaterial() {
        Material m;
        m.color = {0.42f, 0.52f, 0.30f, 1.0f};  // grama
        m.roughness = 0.9f;
        m.specular = 0.3f;
        return m;
    }
};

template <> struct FieldTraits<TerrainRef> {
    static CustomFieldType Describe();
};

} // namespace motor
