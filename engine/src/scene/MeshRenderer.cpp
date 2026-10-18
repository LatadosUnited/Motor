#include "motor/scene/MeshRenderer.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/core/Log.h"

#include <nlohmann/json.hpp>

namespace motor {

using json = nlohmann::ordered_json;

// Gravado como { "guid": "...", "path": "Assets/..." } (ou null); quem manda é o GUID.
CustomFieldType FieldTraits<std::shared_ptr<Material>>::Describe() {
    return {"MaterialRef",
            [](const void* a, const void* b) {
                return *static_cast<const std::shared_ptr<Material>*>(a) == *static_cast<const std::shared_ptr<Material>*>(b);
            },
            [](const void* v, const AssetLibrary& assets) {
                const auto& material = *static_cast<const std::shared_ptr<Material>*>(v);
                if (!material) return std::string("null");
                const auto ref = assets.RefOf(material.get());
                if (!ref) {
                    log::Warn("Salvar cena: material não está no AssetLibrary");
                    return std::string("null");
                }
                json j = {{"guid", ref->guid}};
                if (!ref->name.empty()) j["path"] = ref->name;
                return j.dump();
            },
            [](std::string_view text, void* v, AssetLibrary& assets, std::string* error) {
                auto& material = *static_cast<std::shared_ptr<Material>*>(v);
                const json j = json::parse(text, nullptr, false);
                if (j.is_null()) {
                    material = nullptr;
                    return true;
                }
                std::string guid;
                std::string path;
                if (j.is_string()) {
                    path = j.get<std::string>();
                } else if (j.is_object() && (j.contains("guid") || j.contains("path"))) {
                    guid = j.value("guid", std::string());
                    path = j.value("path", std::string());
                } else {
                    if (error) *error = "esperado caminho \"Assets/...\", objeto {\"path\": \"Assets/...\"} ou null";
                    return false;
                }
                std::string problem;
                std::shared_ptr<Material> loaded = guid.empty() ? nullptr : assets.GetMaterialByGuid(guid, &problem);
                if (!loaded && !path.empty()) {
                    loaded = assets.GetMaterial(path, &problem);
                    if (loaded && !guid.empty()) {
                        log::Warn("Cena: GUID {} não encontrado; usando o material pelo caminho '{}'", guid, path);
                    }
                }
                if (!loaded) {
                    if (error) {
                        *error = problem.empty() ? "material não encontrado" : problem;
                        return false;
                    }
                    log::Warn("Cena: material '{}' não carregado: {}", path, problem);
                }
                material = std::move(loaded);
                return true;
            }};
}

MOTOR_COMPONENT_MODULE(MeshRenderer) {
    // A ordem dos campos é a do Inspector e a do arquivo. Os campos depois de "material" são o material
    // próprio do componente (usado quando não há asset de material). Cenas até a versão 5 tinham "shininess"
    // no lugar de "roughness" (SceneSerializer converte).
    registry.Register<MeshRenderer>("MeshRenderer", "Mesh Renderer")
        .Field("mesh", "Malha", &MeshRenderer::mesh)
        .Field("material", "Material", &MeshRenderer::sharedMaterial,
               {.tooltip = "Asset de material (.mmat), compartilhado entre objetos. Vazio = material próprio."})
        .FieldRef("texture", "Textura", [](MeshRenderer& m) -> auto& { return m.material.albedo; })
        .FieldRef("color", "Cor", [](MeshRenderer& m) -> auto& { return m.material.color; }, {.color = true})
        .FieldRef("metallic", "Metal", [](MeshRenderer& m) -> auto& { return m.material.metallic; },
                  {.slider = true, .min = 0.0f, .max = 1.0f, .tooltip = "0 = não metal (plástico, madeira), 1 = metal"})
        .FieldRef("roughness", "Rugosidade", [](MeshRenderer& m) -> auto& { return m.material.roughness; },
                  {.slider = true, .min = 0.0f, .max = 1.0f, .tooltip = "0 = espelho, 1 = fosco"})
        .FieldRef("specular", "Reflexo", [](MeshRenderer& m) -> auto& { return m.material.specular; },
                  {.slider = true, .min = 0.0f, .max = 1.0f,
                   .tooltip = "Reflexo dos não metais: 0.5 = 4% (o comum), 0 = nenhum"})
        .FieldRef("normalMap", "Mapa de normal", [](MeshRenderer& m) -> auto& { return m.material.normalMap; })
        .FieldRef("normalStrength", "Força da normal", [](MeshRenderer& m) -> auto& { return m.material.normalStrength; },
                  {.slider = true, .min = 0.0f, .max = 2.0f})
        .FieldRef("metallicRoughnessMap", "Mapa metal/rugosidade",
                  [](MeshRenderer& m) -> auto& { return m.material.metallicRoughnessMap; },
                  {.tooltip = "Como no glTF: verde = rugosidade, azul = metal (multiplicam os valores acima)"})
        .FieldRef("occlusionMap", "Mapa de oclusão", [](MeshRenderer& m) -> auto& { return m.material.occlusionMap; })
        .FieldRef("occlusionStrength", "Força da oclusão",
                  [](MeshRenderer& m) -> auto& { return m.material.occlusionStrength; },
                  {.slider = true, .min = 0.0f, .max = 1.0f})
        .FieldRef("emissive", "Emissão", [](MeshRenderer& m) -> auto& { return m.material.emissive; }, {.color = true})
        .FieldRef("emissiveIntensity", "Intensidade da emissão",
                  [](MeshRenderer& m) -> auto& { return m.material.emissiveIntensity; },
                  {.speed = 0.05f, .min = 0.0f, .max = 100.0f, .format = "%.2f"})
        .FieldRef("emissiveMap", "Mapa de emissão", [](MeshRenderer& m) -> auto& { return m.material.emissiveMap; })
        .OnAdd([](MeshRenderer& m, AssetLibrary& assets) { m.mesh = assets.FindMesh("Cubo"); });
}

} // namespace motor
