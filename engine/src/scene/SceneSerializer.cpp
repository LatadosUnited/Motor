#include "motor/scene/SceneSerializer.h"

#include "motor/common/FileWrite.h"
#include "SceneJson.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/scene/Scene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace motor::SceneSerializer {

using namespace detail;

namespace {

// 2: assets de arquivo referenciados por GUID. 3: lista genérica "components" (antes, "meshRenderer" fixo).
// 4: a luz direcional da cena ("light") virou uma entidade com o componente Luz; o ambiente foi para "environment".
// 5: Mesh Renderer com asset de material ("material", .mmat) e instâncias de prefab (componente PrefabInstance).
//    Cenas antigas não têm "material": continuam com o material próprio do componente (cor, textura...).
// 6: renderização da Fase 7. Ambiente com céu (cor ou imagem HDR), luz do céu, sombras e pós-processamento;
//    material PBR no Mesh Renderer (o "shininess" das cenas antigas vira "roughness"); sombra nas luzes.
constexpr int kFormatVersion = 6;

// Até a versão 5 o material do Mesh Renderer era Blinn-Phong: o brilho ("shininess") vira rugosidade.
json MigrateMeshRenderer(const json& obj, int version) {
    if (version >= 6 || !obj.contains("shininess") || obj.contains("roughness")) return obj;
    json out = obj;
    if (obj["shininess"].is_number()) out["roughness"] = Num(RoughnessFromShininess(obj["shininess"].get<float>()));
    out.erase("shininess");
    return out;
}

json AssetRefJson(const std::optional<AssetRef>& ref) {
    if (!ref) return nullptr;
    json j = {{"guid", ref->guid}};
    if (!ref->name.empty()) j["path"] = ref->name;
    return j;
}

json EnvironmentToJson(const Environment& env, const AssetLibrary& assets) {
    std::optional<AssetRef> sky;
    if (env.skyTexture) {
        sky = assets.RefOf(env.skyTexture.get());
        if (!sky) log::Warn("Salvar cena: a imagem do céu não está no AssetLibrary");
    }
    return {
        {"skyColor", Vec(env.skyColor)},
        {"fogDensity", Num(env.fogDensity)},
        {"ambient", Vec(env.ambient)},
        {"sky", SkyModeName(env.skyMode)},
        {"skyTexture", AssetRefJson(sky)},
        {"skyIntensity", Num(env.skyIntensity)},
        {"skyRotation", Num(env.skyRotation)},
        {"skyLight", Num(env.skyLight)},
        {"shadowDistance", Num(env.shadowDistance)},
        {"exposure", Num(env.exposure)},
        {"toneMapper", ToneMapperName(env.toneMapper)},
        {"bloom", env.bloom},
        {"bloomIntensity", Num(env.bloomIntensity)},
        {"bloomThreshold", Num(env.bloomThreshold)},
        {"fxaa", env.fxaa},
    };
}

void EnvironmentFromJson(const json& j, Environment& env, AssetLibrary& assets) {
    Read(j, "skyColor", env.skyColor);
    Read(j, "fogDensity", env.fogDensity);
    Read(j, "ambient", env.ambient);
    if (auto it = j.find("sky"); it != j.end() && it->is_string()) ParseSkyMode(it->get<std::string>(), env.skyMode);
    if (auto it = j.find("skyTexture"); it != j.end() && it->is_object()) {
        const std::string guid = it->value("guid", std::string());
        const std::string path = it->value("path", std::string());
        std::string problem;
        if (!guid.empty()) env.skyTexture = assets.GetEnvironmentMapByGuid(guid, &problem);
        if (!env.skyTexture && !path.empty()) env.skyTexture = assets.GetEnvironmentMap(path, nullptr);  // .meta perdido
        if (!env.skyTexture) log::Warn("Cena: imagem do céu '{}' não carregada: {}", path, problem);
    }
    auto number = [&](const char* key, float& value) {
        if (auto it = j.find(key); it != j.end() && it->is_number()) value = it->get<float>();
    };
    auto boolean = [&](const char* key, bool& value) {
        if (auto it = j.find(key); it != j.end() && it->is_boolean()) value = it->get<bool>();
    };
    number("skyIntensity", env.skyIntensity);
    number("skyRotation", env.skyRotation);
    number("skyLight", env.skyLight);
    number("shadowDistance", env.shadowDistance);
    number("exposure", env.exposure);
    if (auto it = j.find("toneMapper"); it != j.end() && it->is_string()) ParseToneMapper(it->get<std::string>(), env.toneMapper);
    boolean("bloom", env.bloom);
    number("bloomIntensity", env.bloomIntensity);
    number("bloomThreshold", env.bloomThreshold);
    boolean("fxaa", env.fxaa);
}

// Versões 1 a 3: a cena tinha uma luz direcional fixa ("light", com o ambiente junto), sempre presente
// (campo ausente = valor padrão). Vira uma entidade com o componente Luz, no fim da lista.
void ConvertSceneLight(const json& root, Scene& out) {
    // Valores padrão da antiga DirectionalLight.
    glm::vec3 direction{-0.4f, -1.0f, -0.3f};
    glm::vec3 color{1.0f, 0.97f, 0.9f};
    float intensity = 1.0f;
    if (auto it = root.find("light"); it != root.end() && it->is_object()) {
        Read(*it, "direction", direction);
        Read(*it, "color", color);
        Read(*it, "intensity", intensity);
        Read(*it, "ambient", out.environment.ambient);
    }

    Entity& sun = out.CreateEntity("Luz direcional");
    sun.transform.position = {0.0f, 3.0f, 0.0f};  // só para o ícone ficar à vista: a posição não muda a luz
    sun.transform.rotation = RotationLookingAt(direction);
    LightComponent& light = sun.Add<LightComponent>();
    light.type = LightType::Directional;
    light.color = color;
    light.intensity = intensity;
    log::Info("Cena antiga (versão {}): luz da cena convertida na entidade '{}'", root.value("version", 0), sun.name);
}
} // namespace

std::string ToJson(const Scene& scene, const AssetLibrary& assets, const JsonOptions& options) {
    json root;
    const bool prefab = std::string_view(options.format) == kPrefabFormat;
    root["format"] = options.format;
    root["version"] = kFormatVersion;

    if (!prefab) {
        root["environment"] = EnvironmentToJson(scene.environment, assets);
    }

    json entities = json::array();
    for (const auto& e : scene.Entities()) {
        const Transform& t = e->transform;
        json je = {
            {"id", e->id},
            {"name", e->name},
            {"active", e->active},
            {"parent", e->parent},
            {"transform", {
                {"position", Vec(t.position)},
                {"rotation", json::array({Num(t.rotation.x), Num(t.rotation.y), Num(t.rotation.z), Num(t.rotation.w)})},
                {"scale", Vec(t.scale)},
            }},
        };

        if (!e->components.empty()) {
            json components = json::array();
            for (const ComponentPtr& c : e->components) components.push_back(ComponentToJson(c, assets, options, e->name));
            je["components"] = std::move(components);
        }
        entities.push_back(std::move(je));
    }
    root["entities"] = std::move(entities);

    // "replace" troca bytes UTF-8 inválidos em vez de lançar exceção.
    return CompactNumberArrays(root.dump(2, ' ', false, json::error_handler_t::replace));
}

bool FromJson(std::string_view text, Scene& out, AssetLibrary& assets, std::string* error, const char* format) {
    try {
        const json root = json::parse(text);

        const bool prefab = std::string_view(format) == kPrefabFormat;
        if (!root.is_object() || root.value("format", std::string()) != format) {
            SetError(error, prefab ? "O arquivo não é um prefab do Motor." : "O arquivo não é uma cena do Motor.");
            return false;
        }
        const int version = root.value("version", 0);
        if (version < 1 || version > kFormatVersion) {
            SetError(error, std::format("Versão de cena não suportada: {} (esperado até {}).", version, kFormatVersion));
            return false;
        }

        if (auto it = root.find("environment"); it != root.end() && it->is_object()) {
            EnvironmentFromJson(*it, out.environment, assets);
        }

        // 1ª passada: cria tudo na ordem do arquivo. Os ids são reatribuídos, então guardamos o mapeamento.
        std::unordered_map<EntityId, EntityId> idMap;
        std::vector<std::pair<EntityId, EntityId>> parentLinks;  // (novo id, id antigo do pai)

        for (const json& je : root.value("entities", json::array())) {
            Entity& e = out.CreateEntity(je.value("name", std::string("Entidade")));
            Read(je, "active", e.active);

            if (auto t = je.find("transform"); t != je.end() && t->is_object()) {
                Read(*t, "position", e.transform.position);
                Read(*t, "rotation", e.transform.rotation);
                Read(*t, "scale", e.transform.scale);
            }

            // Versões 1 e 2: o único componente era o "meshRenderer", com os mesmos nomes de campo de hoje.
            if (auto m = je.find("meshRenderer"); m != je.end() && m->is_object()) {
                ComponentFromJson(*ComponentRegistry::TypeOf<MeshRenderer>(), MigrateMeshRenderer(*m, version), e, assets);
            }
            if (auto list = je.find("components"); list != je.end() && list->is_array()) {
                for (const json& jc : *list) {
                    if (!jc.is_object()) continue;
                    const std::string typeName = jc.value("type", std::string());
                    const ComponentType* type = ComponentRegistry::Instance().Find(typeName);
                    if (!type) {
                        // Script que ainda não foi compilado/carregado, ou que foi removido: os dados ficam guardados.
                        log::Warn("Cena: componente desconhecido '{}' em '{}' (dados mantidos)", typeName, e.name);
                        e.components.push_back(MakeMissing(typeName, jc));
                        MarkSceneStructureChanged();
                    } else if (e.Get(*type)) {
                        log::Warn("Cena: componente '{}' repetido em '{}' foi ignorado", typeName, e.name);
                    } else if (type == ComponentRegistry::TypeOf<MeshRenderer>()) {
                        ComponentFromJson(*type, MigrateMeshRenderer(jc, version), e, assets);
                    } else {
                        ComponentFromJson(*type, jc, e, assets);
                    }
                }
            }

            const EntityId oldId = je.value("id", EntityId{kNullEntity});
            if (oldId != kNullEntity) idMap[oldId] = e.id;
            const EntityId oldParent = je.value("parent", EntityId{kNullEntity});
            if (oldParent != kNullEntity) parentLinks.emplace_back(e.id, oldParent);
        }

        // 2ª passada: hierarquia. O Transform salvo já é local, então não recalculamos o mundo.
        for (const auto& [child, oldParent] : parentLinks) {
            auto it = idMap.find(oldParent);
            if (it == idMap.end() || !out.SetParent(child, it->second, false)) {
                log::Warn("Cena: pai inválido para '{}'; movida para a raiz", out.Find(child)->name);
            }
        }

        if (version < 4 && !prefab) ConvertSceneLight(root, out);
        return true;
    } catch (const json::exception& e) {
        SetError(error, std::format("JSON inválido: {}", e.what()));
        return false;
    }
}

size_t DetachComponents(Scene& scene, const std::function<bool(const ComponentType&)>& which,
                        const AssetLibrary& assets) {
    size_t count = 0;
    for (const auto& e : scene.Entities()) {
        for (ComponentPtr& component : e->components) {
            const ComponentType& type = component.Type();
            if (&type == &ComponentRegistry::MissingType() || !which(type)) continue;
            component = MakeMissing(type.name, ComponentToJson(component, assets, {}, e->name));
            ++count;
        }
    }
    return count;
}

size_t ResolveMissingComponents(Scene& scene, AssetLibrary& assets, std::vector<FieldWarning>* warnings) {
    const ComponentType& missingType = ComponentRegistry::MissingType();
    size_t count = 0;
    for (const auto& e : scene.Entities()) {
        for (ComponentPtr& component : e->components) {
            if (&component.Type() != &missingType) continue;
            const auto& missing = *static_cast<const MissingComponent*>(component.Data());
            const ComponentType* type = ComponentRegistry::Instance().Find(missing.type);
            if (!type || e->Get(*type)) continue;  // ainda desconhecido, ou a entidade já tem um desse tipo
            const json obj = json::parse(missing.json, nullptr, false);
            ComponentPtr real(*type);
            if (obj.is_object()) {
                ReadFields(*type, real.Data(), obj, assets, e->name);
                if (warnings) CheckStoredFields(*type, real.Data(), obj, assets, e->name, *warnings);
            }
            component = std::move(real);  // no mesmo lugar da lista (ordem do Inspector)
            ++count;
        }
    }
    return count;
}

bool Save(const std::filesystem::path& path, const Scene& scene, const AssetLibrary& assets, std::string* error,
          const JsonOptions& options) {
    const std::string text = ToJson(scene, assets, options);

    // Escreve num arquivo temporário e troca no fim, para não corromper a cena se algo falhar no meio.
    if (!WriteFileAtomic(path, text, error)) return false;

    log::Info("{}: {}", std::string_view(options.format) == kPrefabFormat ? "Prefab salvo" : "Cena salva", ToUtf8(path));
    return true;
}

bool Load(const std::filesystem::path& path, Scene& out, AssetLibrary& assets, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        SetError(error, std::format("Não foi possível abrir '{}'.", ToUtf8(path)));
        return false;
    }
    std::stringstream ss;
    ss << file.rdbuf();

    if (!FromJson(ss.str(), out, assets, error)) return false;
    log::Info("Cena carregada: {} ({} entidades)", ToUtf8(path), out.Entities().size());
    return true;
}

} // namespace motor::SceneSerializer
