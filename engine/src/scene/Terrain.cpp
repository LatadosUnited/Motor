#include "motor/scene/Terrain.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/scene/MeshRenderer.h"
#include "motor/scene/Scene.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace motor {

TerrainData* TerrainRef::Data() const {
    if (file.empty()) return m_data.get();  // sem arquivo: só o que o editor deu (ou nada)
    if (!m_data || m_loaded != file) {
        if (auto data = TerrainData::Open(file)) {
            m_data = std::move(data);
        } else if (m_loaded != file) {
            m_data.reset();
        }
        m_loaded = file;
    }
    return m_data.get();
}

std::shared_ptr<TerrainData> TerrainRef::SharedData() const {
    Data();
    return m_data;
}

bool SaveTerrains(const Scene& scene, std::string* error,
                  const std::function<void(const std::filesystem::path&)>& discard) {
    bool ok = true;
    std::vector<const TerrainData*> saved;  // duas entidades com o mesmo arquivo: grava uma vez
    for (const EntityId id : scene.EntitiesWith<Terrain>()) {
        const Entity* e = scene.Find(id);
        const Terrain* terrain = e ? e->Get<Terrain>() : nullptr;
        if (!terrain || terrain->terrain.file.empty()) continue;
        TerrainData* data = terrain->terrain.Data();
        if (!data || !data->IsDirty() || std::find(saved.begin(), saved.end(), data) != saved.end()) continue;
        saved.push_back(data);
        std::string failure;
        if (!data->Save(terrain->terrain.file, &failure)) {
            ok = false;
            if (error) *error += (error->empty() ? "" : "; ") + failure;
        } else if (discard) {
            for (const std::filesystem::path& unused : data->UnusedBlockFiles(terrain->terrain.file)) discard(unused);
        }
    }
    return ok;
}

TerrainLayers Terrain::Layers() const {
    const Material& base = Resolved();
    TerrainLayers layers;
    layers.albedo[0] = base.albedo;
    layers.color[0] = base.color;
    layers.tiling[0] = tiling;
    const TerrainLayer* others[3] = {&layer2, &layer3, &layer4};
    for (int i = 0; i < 3; ++i) {
        layers.albedo[i + 1] = others[i]->texture;
        layers.color[i + 1] = others[i]->color;
        layers.tiling[i + 1] = others[i]->tiling;
    }
    return layers;
}

void TerrainRef::SetData(std::shared_ptr<TerrainData> data) const {
    m_data = std::move(data);
    m_loaded = file;
}

// Na cena: {"guid": "...", "path": "Assets/Terrenos/x.mterrain"}, ou null. Ao ler, o GUID manda (o caminho segue o
// arquivo, se ele mudou de lugar com o .meta); GUID desconhecido: vale o caminho. Um texto só é o caminho (MCP).
CustomFieldType FieldTraits<TerrainRef>::Describe() {
    return {"TerrainRef",
            [](const void* a, const void* b) { return *static_cast<const TerrainRef*>(a) == *static_cast<const TerrainRef*>(b); },
            [](const void* v, const AssetLibrary&) {
                const auto& ref = *static_cast<const TerrainRef*>(v);
                if (ref.IsEmpty()) return std::string("null");
                return nlohmann::ordered_json{{"guid", ref.guid}, {"path", ref.path}}.dump();
            },
            [](std::string_view text, void* v, AssetLibrary& assets, std::string* error) {
                const auto json = nlohmann::json::parse(text, nullptr, false);
                auto& ref = *static_cast<TerrainRef*>(v);
                if (json.is_null()) {
                    ref = {};
                    return true;
                }
                if (json.is_string()) {
                    ref = {};
                    ref.path = json.get<std::string>();
                    ref.file = assets.FileForKey(ref.path);
                    std::error_code ec;
                    if (!ref.file.empty() && std::filesystem::is_regular_file(ref.file, ec)) {
                        ref.guid = assets.Database().GuidFor(ref.file);
                    }
                    return true;
                }
                if (!json.is_object() || (!json.contains("guid") && !json.contains("path"))) {
                    if (error) *error = "esperado caminho \"Assets/...\", objeto {\"path\": \"Assets/...\"} ou null";
                    return false;
                }
                ref.guid = json.value("guid", std::string());
                ref.path = json.value("path", std::string());
                ref.file.clear();
                if (!ref.guid.empty()) ref.file = assets.Database().PathFor(ref.guid);
                if (!ref.file.empty()) {
                    if (const std::string key = assets.KeyForFile(ref.file); !key.empty()) ref.path = key;
                } else if (!ref.path.empty()) {
                    ref.file = assets.FileForKey(ref.path);
                }
                return true;
            }};
}

namespace {
const FieldOptions kTilingOptions{.min = 0.1f, .max = 1000.0f, .tooltip = "Metros por repetição da textura"};
} // namespace

MOTOR_COMPONENT_MODULE(Terrain) {
    registry.Register<Terrain>("Terrain", "Terreno")
        .Field("terrain", "Terreno", &Terrain::terrain, {.tooltip = "Arquivo .mterrain com as alturas, um arquivo por bloco"})
        .Field("material", "Material", &Terrain::sharedMaterial,
               {.tooltip = "Asset de material (.mmat). Vazio = cor e textura abaixo"})
        .FieldRef("texture", "Camada 1: textura", [](Terrain& t) -> auto& { return t.material.albedo; },
                  {.tooltip = "Camada 1 (onde nada foi pintado): a textura do material"})
        .FieldRef("color", "Camada 1: cor", [](Terrain& t) -> auto& { return t.material.color; }, {.color = true})
        .Field("tiling", "Camada 1: escala", &Terrain::tiling, kTilingOptions)
        .FieldRef("layer2Texture", "Camada 2: textura", [](Terrain& t) -> auto& { return t.layer2.texture; })
        .FieldRef("layer2Color", "Camada 2: cor", [](Terrain& t) -> auto& { return t.layer2.color; }, {.color = true})
        .FieldRef("layer2Tiling", "Camada 2: escala", [](Terrain& t) -> auto& { return t.layer2.tiling; }, kTilingOptions)
        .FieldRef("layer3Texture", "Camada 3: textura", [](Terrain& t) -> auto& { return t.layer3.texture; })
        .FieldRef("layer3Color", "Camada 3: cor", [](Terrain& t) -> auto& { return t.layer3.color; }, {.color = true})
        .FieldRef("layer3Tiling", "Camada 3: escala", [](Terrain& t) -> auto& { return t.layer3.tiling; }, kTilingOptions)
        .FieldRef("layer4Texture", "Camada 4: textura", [](Terrain& t) -> auto& { return t.layer4.texture; })
        .FieldRef("layer4Color", "Camada 4: cor", [](Terrain& t) -> auto& { return t.layer4.color; }, {.color = true})
        .FieldRef("layer4Tiling", "Camada 4: escala", [](Terrain& t) -> auto& { return t.layer4.tiling; }, kTilingOptions)
        .FieldRef("roughness", "Rugosidade", [](Terrain& t) -> auto& { return t.material.roughness; },
                  {.slider = true, .min = 0.0f, .max = 1.0f, .tooltip = "0 = espelho, 1 = fosco"})
        .Field("shadows", "Sombra", &Terrain::shadows)
        .Field("collision", "Colisão", &Terrain::collision,
               {.tooltip = "Corpo da física com as alturas: o personagem anda nele, raios e corpos batem nele"})
        .Field("friction", "Atrito", &Terrain::friction, {.min = 0.0f, .max = 10.0f})
        .Field("layer", "Camada", &Terrain::layer,
               {.tooltip = "Camada de colisão: quem se choca com quem fica em Projeto > Camadas de colisão"});
}

} // namespace motor
