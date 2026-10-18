#include "motor/scene/SceneSerializer.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/scene/Scene.h"

#include <nlohmann/json.hpp>

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

// ordered_json mantém a ordem de inserção das chaves ("format" e "version" no topo do arquivo).
using json = nlohmann::ordered_json;

namespace {
// 2: assets de arquivo referenciados por GUID. 3: lista genérica "components" (antes, "meshRenderer" fixo).
// 4: a luz direcional da cena ("light") virou uma entidade com o componente Luz; o ambiente foi para "environment".
// 5: Mesh Renderer com asset de material ("material", .mmat) e instâncias de prefab (componente PrefabInstance).
//    Cenas antigas não têm "material": continuam com o material próprio do componente (cor, textura...).
// 6: renderização da Fase 7. Ambiente com céu (cor ou imagem HDR), luz do céu, sombras e pós-processamento;
//    material PBR no Mesh Renderer (o "shininess" das cenas antigas vira "roughness"); sombra nas luzes.
constexpr int kFormatVersion = 6;

// float -> double passando pela representação decimal mais curta, para o arquivo ter "0.6" e não
// "0.6000000238418579".
double Num(float value) {
    return std::stod(std::format("{}", value));
}

json Vec(const glm::vec3& v) { return json::array({Num(v.x), Num(v.y), Num(v.z)}); }
json Vec(const glm::vec4& v) { return json::array({Num(v.x), Num(v.y), Num(v.z), Num(v.w)}); }

// Leitores tolerantes: campo ausente ou com formato errado mantém o valor padrão.
template <int N>
void ReadFloats(const json& obj, const char* key, float* out) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_array() || it->size() != N) return;
    for (int i = 0; i < N; ++i) out[i] = (*it)[i].get<float>();
}

void Read(const json& obj, const char* key, glm::vec3& v) { ReadFloats<3>(obj, key, &v.x); }

void Read(const json& obj, const char* key, glm::quat& q) {
    float xyzw[4] = {q.x, q.y, q.z, q.w};
    ReadFloats<4>(obj, key, xyzw);
    q = glm::quat(xyzw[3], xyzw[0], xyzw[1], xyzw[2]);
    // Só normaliza se precisar: renormalizar um quaternion que já é unitário muda o último bit e a cena
    // deixaria de ser igual depois de salvar e abrir.
    if (std::abs(glm::length(q) - 1.0f) > 1e-5f) q = glm::normalize(q);
}

template <class T>
void Read(const json& obj, const char* key, T& value) {
    value = obj.value(key, value);
}

// O dump indentado do nlohmann põe cada número numa linha. Junta arrays só de números em uma linha:
// [1.0, 2.0, 3.0]. Ignora o conteúdo de strings (nomes podem ter colchetes).
std::string CompactNumberArrays(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    bool inString = false;
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (inString) {
            out += c;
            if (c == '\\') {
                if (i + 1 < in.size()) out += in[++i];
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') inString = true;

        if (c == '[') {
            const size_t close = in.find(']', i);
            if (close != std::string::npos) {
                constexpr const char* kSeparators = ", \r\n";
                const std::string_view body(in.data() + i + 1, close - i - 1);
                const bool numeric = body.find_first_not_of("0123456789+-.eE, \r\n") == std::string_view::npos &&
                                     body.find_first_of("0123456789") != std::string_view::npos;
                if (numeric) {
                    out += '[';
                    bool first = true;
                    size_t pos = 0;
                    while (pos < body.size()) {
                        const size_t start = body.find_first_not_of(kSeparators, pos);
                        if (start == std::string_view::npos) break;
                        const size_t end = body.find_first_of(kSeparators, start);
                        if (!first) out += ", ";
                        out += body.substr(start, end - start);
                        first = false;
                        pos = end;
                    }
                    out += ']';
                    i = close;
                    continue;
                }
            }
        }
        out += c;
    }
    return out;
}

void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

// Embutido: "Cubo". Arquivo: { "guid": "...", "path": "Assets/..." } (o caminho é só para leitura humana e
// plano B; quem manda é o GUID).
json RefToJson(const std::optional<AssetRef>& ref, const JsonOptions& options) {
    if (!ref) return nullptr;
    if (ref->guid.empty()) return ref->name;
    json j = {{"guid", ref->guid}};
    if (options.pathHints && !ref->name.empty()) j["path"] = ref->name;
    return j;
}

// Lê uma referência de malha/textura em qualquer formato: nome embutido, caminho (versão 1) ou {guid, path}.
template <class Asset, class ByKey, class ByGuid>
std::shared_ptr<Asset> ResolveRef(const json& value, ByKey byKey, ByGuid byGuid, std::string& problem) {
    if (value.is_string()) return byKey(value.get<std::string>(), &problem);
    if (!value.is_object()) return nullptr;

    const std::string guid = value.value("guid", std::string());
    const std::string path = value.value("path", std::string());
    if (!guid.empty()) {
        if (auto asset = byGuid(guid, &problem)) return asset;
    }
    if (!path.empty()) {
        // .meta perdido ou GUID desconhecido: tenta pelo caminho que estava salvo.
        if (auto asset = byKey(path, nullptr)) {
            log::Warn("Cena: GUID {} não encontrado; usando o arquivo pelo caminho '{}'", guid, path);
            return asset;
        }
        problem = std::format("'{}' (GUID {}) não foi encontrado no projeto.", path, guid);
    }
    return nullptr;
}

// Lê N floats de um array JSON; formato errado mantém o valor atual.
bool FloatsFrom(const json& value, float* out, size_t n) {
    if (!value.is_array() || value.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (!value[i].is_number()) return false;
    }
    for (size_t i = 0; i < n; ++i) out[i] = value[i].get<float>();
    return true;
}

// --- Componentes: salvos e lidos a partir da descrição dos campos (Component.h) ---------------------

json FieldToJson(const FieldInfo& f, const void* c, const AssetLibrary& assets, const JsonOptions& options,
                 const std::string& owner) {
    switch (f.type) {
        case FieldType::Bool: return f.As<bool>(c);
        case FieldType::Int: return f.As<int>(c);
        case FieldType::Float: return Num(f.As<float>(c));
        case FieldType::Vec2: {
            const glm::vec2& v = f.As<glm::vec2>(c);
            return json::array({Num(v.x), Num(v.y)});
        }
        case FieldType::Vec3: return Vec(f.As<glm::vec3>(c));
        case FieldType::Vec4: return Vec(f.As<glm::vec4>(c));
        case FieldType::String: return f.As<std::string>(c);
        case FieldType::Mesh: {
            const auto& mesh = f.As<std::shared_ptr<Mesh>>(c);
            const auto ref = assets.RefOf(mesh.get());
            if (mesh && !ref) log::Warn("Salvar cena: malha de '{}' não está no AssetLibrary", owner);
            return RefToJson(ref, options);
        }
        case FieldType::Texture: {
            const auto& texture = f.As<std::shared_ptr<Texture2D>>(c);
            const auto ref = assets.RefOf(texture.get());
            if (texture && !ref) log::Warn("Salvar cena: textura de '{}' não está no AssetLibrary", owner);
            return RefToJson(ref, options);
        }
        case FieldType::Custom: {
            json value = json::parse(f.custom->toJson(f.Get(c), assets), nullptr, false);
            if (value.is_discarded()) {
                log::Warn("Salvar cena: campo '{}' de '{}' ({}) gerou JSON inválido", f.key, owner, f.custom->name);
                return nullptr;
            }
            return value;
        }
    }
    return nullptr;
}

void FieldFromJson(const FieldInfo& f, void* c, const json& value, AssetLibrary& assets, const std::string& owner) {
    std::string problem;
    switch (f.type) {
        case FieldType::Bool:
            if (value.is_boolean()) f.As<bool>(c) = value.get<bool>();
            break;
        case FieldType::Int:
            if (value.is_number()) f.As<int>(c) = value.get<int>();
            break;
        case FieldType::Float:
            if (value.is_number()) f.As<float>(c) = value.get<float>();
            break;
        case FieldType::Vec2: FloatsFrom(value, &f.As<glm::vec2>(c).x, 2); break;
        case FieldType::Vec3: FloatsFrom(value, &f.As<glm::vec3>(c).x, 3); break;
        case FieldType::Vec4: FloatsFrom(value, &f.As<glm::vec4>(c).x, 4); break;
        case FieldType::String:
            if (value.is_string()) f.As<std::string>(c) = value.get<std::string>();
            break;
        case FieldType::Mesh: {
            auto& mesh = f.As<std::shared_ptr<Mesh>>(c);
            mesh = nullptr;
            if (value.is_null()) break;
            mesh = ResolveRef<Mesh>(
                value, [&](std::string_view k, std::string* err) { return assets.GetMesh(k, err); },
                [&](std::string_view g, std::string* err) { return assets.GetMeshByGuid(g, err); }, problem);
            if (!mesh) log::Warn("Cena: malha de '{}' não carregada: {}", owner, problem);
            break;
        }
        case FieldType::Texture: {
            auto& texture = f.As<std::shared_ptr<Texture2D>>(c);
            texture = nullptr;
            if (value.is_null()) break;
            texture = ResolveRef<Texture2D>(
                value, [&](std::string_view k, std::string* err) { return assets.GetTexture(k, err); },
                [&](std::string_view g, std::string* err) { return assets.GetTextureByGuid(g, err); }, problem);
            if (!texture) log::Warn("Cena: textura de '{}' não carregada: {}", owner, problem);
            break;
        }
        case FieldType::Custom: f.custom->fromJson(value.dump(), f.Get(c), assets, nullptr); break;
    }
}

json ComponentToJson(const ComponentPtr& component, const AssetLibrary& assets, const JsonOptions& options,
                     const std::string& owner) {
    // Tipo desconhecido: grava de volta exatamente o que foi lido.
    if (&component.Type() == &ComponentRegistry::MissingType()) {
        const auto& missing = *static_cast<const MissingComponent*>(component.Data());
        json stored = json::parse(missing.json, nullptr, false);
        if (stored.is_object()) return stored;
        return json{{"type", missing.type}};
    }
    json j;
    j["type"] = component.Type().name;
    for (const FieldInfo& f : component.Type().fields) {
        j[f.key] = FieldToJson(f, component.Data(), assets, options, owner);
    }
    return j;
}

// Campos ausentes no arquivo ficam com o valor padrão do componente.
// Valor gravado do campo: pelo nome atual ou, se não houver, pelo nome antigo (FieldOptions::formerKey).
json::const_iterator StoredField(const FieldInfo& f, const json& obj) {
    auto it = obj.find(f.key);
    if (it == obj.end() && f.options.formerKey) it = obj.find(f.options.formerKey);
    return it;
}

void ReadFields(const ComponentType& type, void* data, const json& obj, AssetLibrary& assets, const std::string& owner) {
    for (const FieldInfo& f : type.fields) {
        if (auto it = StoredField(f, obj); it != obj.end()) FieldFromJson(f, data, *it, assets, owner);
    }
}

// Mesma "forma" de JSON (número, texto, lista do mesmo tamanho...): o valor gravado serve no campo atual.
bool SameJsonKind(const json& stored, const json& current) {
    if (stored.is_null() || current.is_null()) return true;
    if (stored.is_number() && current.is_number()) return true;
    if (stored.is_array() && current.is_array()) return stored.size() == current.size();
    return stored.type() == current.type();
}

// Depois de ReadFields: o que do JSON guardado não entrou no componente (campo que sumiu ou mudou de tipo).
void CheckStoredFields(const ComponentType& type, void* data, const json& obj, AssetLibrary& assets,
                       const std::string& owner, std::vector<FieldWarning>& warnings) {
    auto warn = [&](const std::string& key, const json& value, bool typeChanged) {
        warnings.push_back({type.name, owner, key, value.dump(-1, ' ', false, json::error_handler_t::replace), typeChanged});
    };
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (it.key() == "type") continue;
        const bool used = std::any_of(type.fields.begin(), type.fields.end(), [&](const FieldInfo& f) {
            return f.key == it.key() || (f.options.formerKey && f.options.formerKey == it.key() && !obj.contains(f.key));
        });
        if (!used) warn(it.key(), it.value(), false);
    }
    for (const FieldInfo& f : type.fields) {
        const auto it = StoredField(f, obj);
        if (it == obj.end()) continue;
        bool fits = true;
        switch (f.type) {
            case FieldType::Mesh:
            case FieldType::Texture: break;  // nome ou {guid, path}: um asset que falta já vira aviso ao ler
            case FieldType::Custom: fits = f.custom->fromJson(it->dump(), f.Get(data), assets, nullptr); break;
            default: fits = SameJsonKind(*it, FieldToJson(f, data, assets, {}, owner)); break;
        }
        if (!fits) warn(it.key(), *it, true);
    }
}

void ComponentFromJson(const ComponentType& type, const json& obj, Entity& e, AssetLibrary& assets) {
    ReadFields(type, e.Add(type), obj, assets, e.name);
}

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

ComponentPtr MakeMissing(std::string typeName, const json& obj) {
    ComponentPtr component(ComponentRegistry::MissingType());
    auto& missing = *static_cast<MissingComponent*>(component.Data());
    missing.type = std::move(typeName);
    missing.json = obj.dump(-1, ' ', false, json::error_handler_t::replace);
    return component;
}
} // namespace

std::string FieldValueToJson(const FieldInfo& field, const void* component, const AssetLibrary& assets) {
    return FieldToJson(field, component, assets, JsonOptions{}, field.key)
        .dump(-1, ' ', false, json::error_handler_t::replace);
}

bool FieldValueFromJson(const FieldInfo& field, void* component, std::string_view text, AssetLibrary& assets,
                        std::string* error) {
    const json value = json::parse(text, nullptr, false);
    if (value.is_discarded()) {
        SetError(error, "valor não é JSON válido: " + std::string(text));
        return false;
    }
    switch (field.type) {
        case FieldType::Bool:
            if (!value.is_boolean()) {
                SetError(error, std::format("campo '{}' espera bool (true ou false)", field.key));
                return false;
            }
            field.As<bool>(component) = value.get<bool>();
            return true;
        case FieldType::Int:
            if (!value.is_number_integer()) {
                SetError(error, std::format("campo '{}' espera int (número inteiro, ex.: 0)", field.key));
                return false;
            }
            field.As<int>(component) = value.get<int>();
            return true;
        case FieldType::Float:
            if (!value.is_number()) {
                SetError(error, std::format("campo '{}' espera float (número, ex.: 1.0)", field.key));
                return false;
            }
            field.As<float>(component) = value.get<float>();
            return true;
        case FieldType::Vec2:
            if (!FloatsFrom(value, &field.As<glm::vec2>(component).x, 2)) {
                SetError(error, std::format("campo '{}' espera vec2 (lista de 2 números, ex.: [0, 0])", field.key));
                return false;
            }
            return true;
        case FieldType::Vec3:
            if (!FloatsFrom(value, &field.As<glm::vec3>(component).x, 3)) {
                SetError(error, std::format("campo '{}' espera vec3 (lista de 3 números, ex.: [0, 0, 0])", field.key));
                return false;
            }
            return true;
        case FieldType::Vec4:
            if (!FloatsFrom(value, &field.As<glm::vec4>(component).x, 4)) {
                SetError(error, std::format("campo '{}' espera vec4 (lista de 4 números, ex.: [1, 1, 1, 1])", field.key));
                return false;
            }
            return true;
        case FieldType::String:
            if (!value.is_string()) {
                SetError(error, std::format("campo '{}' espera string (texto entre aspas)", field.key));
                return false;
            }
            field.As<std::string>(component) = value.get<std::string>();
            return true;
        case FieldType::Mesh: {
            auto& mesh = field.As<std::shared_ptr<Mesh>>(component);
            if (value.is_null()) {
                mesh = nullptr;
                return true;
            }
            if (!value.is_string() && !(value.is_object() && (value.contains("guid") || value.contains("path")))) {
                SetError(error, std::format("campo '{}' espera mesh (nome embutido como \"Cubo\", caminho \"Assets/...\", "
                                            "objeto {{\"path\": \"Assets/...\"}} ou null)",
                                            field.key));
                return false;
            }
            std::string problem;
            auto loaded = ResolveRef<Mesh>(
                value, [&](std::string_view k, std::string* err) { return assets.GetMesh(k, err); },
                [&](std::string_view g, std::string* err) { return assets.GetMeshByGuid(g, err); }, problem);
            if (!loaded) {
                SetError(error, std::format("campo '{}': {}", field.key, problem.empty() ? "malha não encontrada" : problem));
                return false;
            }
            mesh = std::move(loaded);
            return true;
        }
        case FieldType::Texture: {
            auto& texture = field.As<std::shared_ptr<Texture2D>>(component);
            if (value.is_null()) {
                texture = nullptr;
                return true;
            }
            if (!value.is_string() && !(value.is_object() && (value.contains("guid") || value.contains("path")))) {
                SetError(error, std::format("campo '{}' espera texture (nome embutido como \"Branco\", caminho "
                                            "\"Assets/...\", objeto {{\"path\": \"Assets/...\"}} ou null)",
                                            field.key));
                return false;
            }
            std::string problem;
            auto loaded = ResolveRef<Texture2D>(
                value, [&](std::string_view k, std::string* err) { return assets.GetTexture(k, err); },
                [&](std::string_view g, std::string* err) { return assets.GetTextureByGuid(g, err); }, problem);
            if (!loaded) {
                SetError(error,
                         std::format("campo '{}': {}", field.key, problem.empty() ? "textura não encontrada" : problem));
                return false;
            }
            texture = std::move(loaded);
            return true;
        }
        case FieldType::Custom: {
            std::string problem;
            if (!field.custom->fromJson(text, field.Get(component), assets, &problem)) {
                SetError(error, std::format("campo '{}' ({}): {}", field.key, field.custom->name,
                                            problem.empty() ? "valor inválido" : problem));
                return false;
            }
            return true;
        }
    }
    return false;
}

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
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file || !(file << text) || !file.flush()) {
            SetError(error, std::format("Não foi possível escrever '{}'.", ToUtf8(temp)));
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        // Algumas implementações não substituem o destino existente.
        std::filesystem::remove(path, ec);
        std::filesystem::rename(temp, path, ec);
    }
    if (ec) {
        SetError(error, std::format("Não foi possível salvar '{}': {}", ToUtf8(path), ec.message()));
        return false;
    }

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

namespace {
constexpr const char* kRecoveryFormat = "motor-recovery";
constexpr int kRecoveryVersion = 1;

std::filesystem::path RecoveryDir(const std::filesystem::path& projectRoot) {
    return projectRoot / ".motor" / "recuperacao";
}
} // namespace

bool SaveRecovery(const std::filesystem::path& projectRoot, const Scene& scene, const AssetLibrary& assets,
                  const std::filesystem::path& originalScenePath, std::string_view reason, std::string* error) {
    if (projectRoot.empty()) {
        SetError(error, "Sem pasta de projeto para gravar a recuperação.");
        return false;
    }
    std::error_code ec;
    const std::filesystem::path dir = RecoveryDir(projectRoot);
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        SetError(error, std::format("Não foi possível criar '{}': {}", ToUtf8(dir), ec.message()));
        return false;
    }

    const std::filesystem::path sceneFile = dir / "cena.mscene";
    const std::string text = ToJson(scene, assets);
    std::filesystem::path tempScene = sceneFile;
    tempScene += ".tmp";
    {
        std::ofstream out(tempScene, std::ios::binary | std::ios::trunc);
        if (!out || !(out << text) || !out.flush()) {
            SetError(error, std::format("Não foi possível escrever '{}'.", ToUtf8(tempScene)));
            return false;
        }
    }
    std::filesystem::rename(tempScene, sceneFile, ec);
    if (ec) {
        std::filesystem::remove(sceneFile, ec);
        std::filesystem::rename(tempScene, sceneFile, ec);
    }
    if (ec) {
        SetError(error, std::format("Não foi possível salvar '{}': {}", ToUtf8(sceneFile), ec.message()));
        return false;
    }

    const auto now = std::chrono::system_clock::now();
    const int64_t savedAt = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::string relScene;
    if (!originalScenePath.empty()) {
        relScene = IsInside(originalScenePath, projectRoot)
                       ? ToGenericUtf8(originalScenePath.lexically_normal().lexically_relative(projectRoot))
                       : ToUtf8(originalScenePath);
    }
    json meta;
    meta["format"] = kRecoveryFormat;
    meta["version"] = kRecoveryVersion;
    meta["scenePath"] = relScene;
    meta["reason"] = std::string(reason);
    meta["savedAt"] = savedAt;

    const std::filesystem::path metaFile = dir / "recuperacao.json";
    std::filesystem::path tempMeta = metaFile;
    tempMeta += ".tmp";
    {
        std::ofstream out(tempMeta, std::ios::binary | std::ios::trunc);
        if (!out || !(out << meta.dump(2) << '\n') || !out.flush()) {
            SetError(error, std::format("Não foi possível escrever '{}'.", ToUtf8(tempMeta)));
            return false;
        }
    }
    std::filesystem::rename(tempMeta, metaFile, ec);
    if (ec) {
        std::filesystem::remove(metaFile, ec);
        std::filesystem::rename(tempMeta, metaFile, ec);
    }
    if (ec) {
        SetError(error, std::format("Não foi possível salvar '{}': {}", ToUtf8(metaFile), ec.message()));
        return false;
    }
    return true;
}

std::optional<RecoveryInfo> CheckRecovery(const std::filesystem::path& projectRoot) {
    if (projectRoot.empty()) return std::nullopt;
    std::error_code ec;
    const std::filesystem::path dir = RecoveryDir(projectRoot);
    const std::filesystem::path sceneFile = dir / "cena.mscene";
    const std::filesystem::path metaFile = dir / "recuperacao.json";
    if (!std::filesystem::is_regular_file(sceneFile, ec) || !std::filesystem::is_regular_file(metaFile, ec)) {
        return std::nullopt;
    }
    std::ifstream in(metaFile, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    const json meta = json::parse(ss.str(), nullptr, false);
    if (!meta.is_object() || meta.value("format", std::string()) != kRecoveryFormat) return std::nullopt;

    RecoveryInfo info;
    info.sceneFile = sceneFile;
    const std::string savedPath = meta.value("scenePath", std::string());
    if (!savedPath.empty()) {
        const std::filesystem::path p = PathFromUtf8(savedPath);
        info.originalPath = (p.is_absolute() ? p : projectRoot / p).lexically_normal();
    }
    info.reason = meta.value("reason", std::string("autosave"));
    info.savedAt = meta.value("savedAt", int64_t{0});
    return info;
}

void ClearRecovery(const std::filesystem::path& projectRoot) {
    if (projectRoot.empty()) return;
    std::error_code ec;
    std::filesystem::remove_all(RecoveryDir(projectRoot), ec);
}

} // namespace motor::SceneSerializer
