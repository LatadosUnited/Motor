#include "motor/scene/SceneSerializer.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/scene/Scene.h"

#include <nlohmann/json.hpp>

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
constexpr const char* kFormatName = "motor-scene";
// 2: assets de arquivo referenciados por GUID. 3: lista genérica "components" (antes, "meshRenderer" fixo).
// 4: a luz direcional da cena ("light") virou uma entidade com o componente Luz; o ambiente foi para "environment".
constexpr int kFormatVersion = 4;

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
        case FieldType::Custom: f.custom->fromJson(value.dump(), f.Get(c), assets); break;
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
void ReadFields(const ComponentType& type, void* data, const json& obj, AssetLibrary& assets, const std::string& owner) {
    for (const FieldInfo& f : type.fields) {
        if (auto it = obj.find(f.key); it != obj.end()) FieldFromJson(f, data, *it, assets, owner);
    }
}

void ComponentFromJson(const ComponentType& type, const json& obj, Entity& e, AssetLibrary& assets) {
    ReadFields(type, e.Add(type), obj, assets, e.name);
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
    sun.transform.position = {0.0f, 10.0f, 0.0f};  // só para o ícone ficar à vista: a posição não muda a luz
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

std::string ToJson(const Scene& scene, const AssetLibrary& assets, const JsonOptions& options) {
    json root;
    root["format"] = kFormatName;
    root["version"] = kFormatVersion;

    root["environment"] = {
        {"skyColor", Vec(scene.environment.skyColor)},
        {"fogDensity", Num(scene.environment.fogDensity)},
        {"ambient", Vec(scene.environment.ambient)},
    };

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

bool FromJson(std::string_view text, Scene& out, AssetLibrary& assets, std::string* error) {
    try {
        const json root = json::parse(text);

        if (!root.is_object() || root.value("format", std::string()) != kFormatName) {
            SetError(error, "O arquivo não é uma cena do Motor.");
            return false;
        }
        const int version = root.value("version", 0);
        if (version < 1 || version > kFormatVersion) {
            SetError(error, std::format("Versão de cena não suportada: {} (esperado até {}).", version, kFormatVersion));
            return false;
        }

        if (auto it = root.find("environment"); it != root.end() && it->is_object()) {
            Read(*it, "skyColor", out.environment.skyColor);
            Read(*it, "fogDensity", out.environment.fogDensity);
            Read(*it, "ambient", out.environment.ambient);
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
                ComponentFromJson(*ComponentRegistry::TypeOf<MeshRenderer>(), *m, e, assets);
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

        if (version < 4) ConvertSceneLight(root, out);
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

size_t ResolveMissingComponents(Scene& scene, AssetLibrary& assets) {
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
            if (obj.is_object()) ReadFields(*type, real.Data(), obj, assets, e->name);
            component = std::move(real);  // no mesmo lugar da lista (ordem do Inspector)
            ++count;
        }
    }
    return count;
}

bool Save(const std::filesystem::path& path, const Scene& scene, const AssetLibrary& assets, std::string* error) {
    const std::string text = ToJson(scene, assets);

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

    log::Info("Cena salva: {}", ToUtf8(path));
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
