#pragma once

// Terreno (Fase 28): liga um arquivo .mterrain (mapa de altura em blocos, motor/assets/TerrainData.h) à entidade. A
// cena desenha cada bloco como um objeto estático (Scene::SubmitTo), na posição da entidade, e o Raycast do editor
// acerta os blocos. Pintura (Tarefa 10): até 4 camadas de textura, misturadas pelos pesos de cada amostra do
// terreno; a camada 1 é a textura e a cor do material, as outras ficam no componente. Física (Tarefa 11): com
// `collision`, a camada física põe um height field da Jolt por bloco (corpo estático da entidade: raios, contatos e o
// personagem acertam o terreno), sem precisar de Collider. LOD vem depois.

#include "motor/assets/TerrainData.h"
#include "motor/renderer/Material.h"
#include "motor/scene/CollisionLayerField.h"
#include "motor/scene/Component.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace motor {

class Scene;

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
    // O mesmo, dividindo a posse (o desfazer guarda as alturas que mudou, mesmo que a entidade saia da cena).
    std::shared_ptr<TerrainData> SharedData() const;
    // Troca as alturas em memória (o editor, ao criar um terreno novo, antes de qualquer leitura do disco).
    void SetData(std::shared_ptr<TerrainData> data) const;

private:
    mutable std::shared_ptr<TerrainData> m_data;
    mutable std::filesystem::path m_loaded;  // de onde veio m_data: outro `file` abre de novo
};

// Camada de textura pintável (2 a 4; a 1 é o material).
struct TerrainLayer {
    std::shared_ptr<Texture2D> texture;  // nula = só a cor
    glm::vec4 color{1.0f};
    float tiling = 4.0f;  // metros por repetição da textura
};

struct Terrain {
    TerrainRef terrain;
    std::shared_ptr<Material> sharedMaterial;  // nulo = usa `material`
    Material material = DefaultMaterial();
    float tiling = 4.0f;  // camada 1: metros por repetição da textura do material
    // Cores padrão que se distinguem sem textura: terra, rocha e areia.
    TerrainLayer layer2{nullptr, {0.42f, 0.32f, 0.22f, 1.0f}};
    TerrainLayer layer3{nullptr, {0.48f, 0.47f, 0.45f, 1.0f}};
    TerrainLayer layer4{nullptr, {0.80f, 0.74f, 0.55f, 1.0f}};
    ShadowCasting shadows = ShadowCasting::On;
    bool collision = true;   // corpo da física (height field), sem Collider
    float friction = 0.6f;
    CollisionLayer layer;    // camada de colisão do projeto

    const Material& Resolved() const { return sharedMaterial ? *sharedMaterial : material; }
    // As 4 camadas como o renderer recebe, sem os pesos e o canto do bloco (a cena põe os de cada bloco).
    TerrainLayers Layers() const;

    static Material DefaultMaterial() {
        Material m;
        m.color = {0.42f, 0.52f, 0.30f, 1.0f};  // grama
        m.roughness = 0.9f;
        m.specular = 0.3f;
        return m;
    }
};

// Grava as alturas mudadas de cada terreno da cena no arquivo dele (TerrainData::Save; terreno sem arquivo fica só na
// memória). Continua nos outros se um falhar; `error` junta as falhas. Com `discard`, depois de gravar, os arquivos
// dos blocos tirados (TerrainData::UnusedBlockFiles) vão para ela (o editor manda para a Lixeira).
bool SaveTerrains(const Scene& scene, std::string* error = nullptr,
                  const std::function<void(const std::filesystem::path&)>& discard = {});

template <> struct FieldTraits<TerrainRef> {
    static CustomFieldType Describe();
};

} // namespace motor
