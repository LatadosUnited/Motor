// Cena em JSON: campos e componentes salvos e lidos pela descrição dos campos (Component.h), e as peças
// comuns de SceneJson.h.

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
// Valor gravado do campo: pelo nome atual ou, se não houver, pelo nome antigo (FieldOptions::formerKey).
json::const_iterator StoredField(const FieldInfo& f, const json& obj) {
    auto it = obj.find(f.key);
    if (it == obj.end() && f.options.formerKey) it = obj.find(f.options.formerKey);
    return it;
}
// Mesma "forma" de JSON (número, texto, lista do mesmo tamanho...): o valor gravado serve no campo atual.
bool SameJsonKind(const json& stored, const json& current) {
    if (stored.is_null() || current.is_null()) return true;
    if (stored.is_number() && current.is_number()) return true;
    if (stored.is_array() && current.is_array()) return stored.size() == current.size();
    return stored.type() == current.type();
}
} // namespace

namespace detail {

double Num(float value) {
    return std::stod(std::format("{}", value));
}

json Vec(const glm::vec3& v) { return json::array({Num(v.x), Num(v.y), Num(v.z)}); }
json Vec(const glm::vec4& v) { return json::array({Num(v.x), Num(v.y), Num(v.z), Num(v.w)}); }

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

void ReadFields(const ComponentType& type, void* data, const json& obj, AssetLibrary& assets, const std::string& owner) {
    for (const FieldInfo& f : type.fields) {
        if (auto it = StoredField(f, obj); it != obj.end()) FieldFromJson(f, data, *it, assets, owner);
    }
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

ComponentPtr MakeMissing(std::string typeName, const json& obj) {
    ComponentPtr component(ComponentRegistry::MissingType());
    auto& missing = *static_cast<MissingComponent*>(component.Data());
    missing.type = std::move(typeName);
    missing.json = obj.dump(-1, ' ', false, json::error_handler_t::replace);
    return component;
}

} // namespace detail

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

} // namespace motor::SceneSerializer
