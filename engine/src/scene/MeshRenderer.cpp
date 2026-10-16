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
            [](std::string_view text, void* v, AssetLibrary& assets) {
                auto& material = *static_cast<std::shared_ptr<Material>*>(v);
                const json j = json::parse(text, nullptr, false);
                if (j.is_null()) {
                    material = nullptr;
                    return;
                }
                if (!j.is_object()) return;
                const std::string guid = j.value("guid", std::string());
                const std::string path = j.value("path", std::string());
                std::string problem;
                std::shared_ptr<Material> loaded = guid.empty() ? nullptr : assets.GetMaterialByGuid(guid, &problem);
                if (!loaded && !path.empty()) {
                    loaded = assets.GetMaterial(path, nullptr);  // .meta perdido: tenta pelo caminho salvo
                    if (loaded) log::Warn("Cena: GUID {} não encontrado; usando o material pelo caminho '{}'", guid, path);
                }
                if (!loaded) log::Warn("Cena: material '{}' não carregado: {}", path, problem);
                material = std::move(loaded);
            }};
}

MOTOR_COMPONENT_MODULE(MeshRenderer) {
    // A ordem dos campos é a do Inspector e a do arquivo. Os campos depois de "material" são o material
    // próprio do componente (usado quando não há asset de material).
    registry.Register<MeshRenderer>("MeshRenderer", "Mesh Renderer")
        .Field("mesh", "Malha", &MeshRenderer::mesh)
        .Field("material", "Material", &MeshRenderer::sharedMaterial,
               {.tooltip = "Asset de material (.mmat), compartilhado entre objetos. Vazio = material próprio."})
        .FieldRef("texture", "Textura", [](MeshRenderer& m) -> auto& { return m.material.albedo; })
        .FieldRef("color", "Cor", [](MeshRenderer& m) -> auto& { return m.material.color; }, {.color = true})
        .FieldRef("specular", "Especular", [](MeshRenderer& m) -> auto& { return m.material.specular; },
                  {.slider = true, .min = 0.0f, .max = 2.0f})
        .FieldRef("shininess", "Brilho", [](MeshRenderer& m) -> auto& { return m.material.shininess; },
                  {.slider = true, .logarithmic = true, .min = 1.0f, .max = 256.0f, .format = "%.0f"})
        .OnAdd([](MeshRenderer& m, AssetLibrary& assets) { m.mesh = assets.FindMesh("Cubo"); });
}

} // namespace motor
