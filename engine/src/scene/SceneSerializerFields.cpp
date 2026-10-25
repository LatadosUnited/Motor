// Cena em JSON: campos e componentes salvos e lidos pela descrição dos campos (Component.h), e as peças
// comuns de SceneJson.h.

#include "SceneJson.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/scene/Scene.h"

#include <algorithm>
#include <array>
#include <charconv>
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

// Os valores que os campos Custom mais devolvem (o nome de um enum, um número inteiro, null), sem o json::parse:
// numa cena com milhares de entidades o parse de cada um custava dezenas de ms (Fase 27). Outros: nullopt.
std::optional<json> SimpleJsonValue(std::string_view text) {
    if (text == "null") return json(nullptr);
    if (text == "true") return json(true);
    if (text == "false") return json(false);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        const std::string_view inner = text.substr(1, text.size() - 2);
        for (const char ch : inner) {
            if (ch == '"' || ch == '\\' || static_cast<unsigned char>(ch) < 0x20) return std::nullopt;  // escapes
        }
        return json(std::string(inner));
    }
    if (!text.empty() && text.size() <= 18) {
        // Inteiro como o parse o leria: sem sinal vira unsigned, com '-' vira int64.
        const bool negative = text.front() == '-';
        const std::string_view digits = negative ? text.substr(1) : text;
        if (digits.empty() || digits.find_first_not_of("0123456789") != std::string_view::npos) return std::nullopt;
        if (digits.size() > 1 && digits.front() == '0') return std::nullopt;  // JSON não aceita zero à esquerda
        if (negative) {
            int64_t v = 0;
            std::from_chars(text.data(), text.data() + text.size(), v);
            return json(v);
        }
        uint64_t v = 0;
        std::from_chars(digits.data(), digits.data() + digits.size(), v);
        return json(v);
    }
    return std::nullopt;
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
            const std::string text = f.custom->toJson(f.Get(c), assets);
            if (std::optional<json> simple = SimpleJsonValue(text)) return std::move(*simple);
            json value = json::parse(text, nullptr, false);
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

// Campo de malha ou textura a partir de um valor JSON (FieldValueFromJson): null limpa; nome embutido, caminho ou
// {guid, path} carrega pelo AssetLibrary. `typeName` e `example` entram na mensagem de erro.
template <class Asset, class ByKey, class ByGuid>
bool AssetFieldFromValue(const FieldInfo& field, std::shared_ptr<Asset>& slot, const json& value, const char* typeName,
                         const char* example, const char* notFound, ByKey byKey, ByGuid byGuid, std::string* error) {
    if (value.is_null()) {
        slot = nullptr;
        return true;
    }
    if (!value.is_string() && !(value.is_object() && (value.contains("guid") || value.contains("path")))) {
        SetError(error, std::format("campo '{}' espera {} (nome embutido como \"{}\", caminho \"Assets/...\", "
                                    "objeto {{\"path\": \"Assets/...\"}} ou null)",
                                    field.key, typeName, example));
        return false;
    }
    std::string problem;
    auto loaded = ResolveRef<Asset>(value, byKey, byGuid, problem);
    if (!loaded) {
        SetError(error, std::format("campo '{}': {}", field.key, problem.empty() ? notFound : problem));
        return false;
    }
    slot = std::move(loaded);
    return true;
}
} // namespace

namespace detail {

double Num(float value) {
    // to_chars dá a mesma representação mais curta do std::format, sem o std::stod (lento no MinGW: passa pelo
    // locale); a cena de referência tem ~100 mil números (Fase 27).
    char text[32];
    const char* end = std::to_chars(text, text + sizeof(text), value).ptr;
    double out = 0.0;
    std::from_chars(text, end, out);
    return out;
}

json Vec(const glm::vec3& v) { return json::array({Num(v.x), Num(v.y), Num(v.z)}); }
json Vec(const glm::vec4& v) { return json::array({Num(v.x), Num(v.y), Num(v.z), Num(v.w)}); }

// O dump indentado do nlohmann põe cada número numa linha. Junta arrays só de números em uma linha:
// [1.0, 2.0, 3.0]. Ignora o conteúdo de strings (nomes podem ter colchetes).
std::string CompactNumberArrays(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    bool inString = false;
    // Copia em trechos até o próximo caractere que importa (aspas e barra na string, aspas e '[' fora dela): a
    // cópia caractere a caractere levava ~40 ms numa cena de 5 MB (Fase 27).
    for (size_t i = 0; i < in.size(); ++i) {
        size_t stop = i;
        if (inString) {
            while (stop < in.size() && in[stop] != '"' && in[stop] != '\\') ++stop;
        } else {
            while (stop < in.size() && in[stop] != '"' && in[stop] != '[') ++stop;
        }
        out.append(in, i, stop - i);
        if (stop == in.size()) break;
        i = stop;
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
                // Tabela em vez do find_first_not_of com 17 caracteres (que testa um a um): metade do tempo da função.
                static const auto kNumberChars = [] {
                    std::array<bool, 256> table{};
                    for (const char ch : std::string_view("0123456789+-.eE, \r\n")) table[static_cast<unsigned char>(ch)] = true;
                    return table;
                }();
                bool numeric = false;  // só caracteres de número e ao menos um dígito
                for (const char ch : body) {
                    if (!kNumberChars[static_cast<unsigned char>(ch)]) {
                        numeric = false;
                        break;
                    }
                    numeric = numeric || (ch >= '0' && ch <= '9');
                }
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
        case FieldType::Mesh:
            return AssetFieldFromValue(
                field, field.As<std::shared_ptr<Mesh>>(component), value, "mesh", "Cubo", "malha não encontrada",
                [&](std::string_view k, std::string* err) { return assets.GetMesh(k, err); },
                [&](std::string_view g, std::string* err) { return assets.GetMeshByGuid(g, err); }, error);
        case FieldType::Texture:
            return AssetFieldFromValue(
                field, field.As<std::shared_ptr<Texture2D>>(component), value, "texture", "Branco",
                "textura não encontrada",
                [&](std::string_view k, std::string* err) { return assets.GetTexture(k, err); },
                [&](std::string_view g, std::string* err) { return assets.GetTextureByGuid(g, err); }, error);
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
