#include "motor/scene/SceneSerializer.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/scene/Scene.h"

#include <nlohmann/json.hpp>

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
constexpr int kFormatVersion = 2;  // 2: assets de arquivo referenciados por GUID

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
void Read(const json& obj, const char* key, glm::vec4& v) { ReadFloats<4>(obj, key, &v.x); }

void Read(const json& obj, const char* key, glm::quat& q) {
    float xyzw[4] = {q.x, q.y, q.z, q.w};
    ReadFloats<4>(obj, key, xyzw);
    q = glm::normalize(glm::quat(xyzw[3], xyzw[0], xyzw[1], xyzw[2]));
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
} // namespace

std::string ToJson(const Scene& scene, const AssetLibrary& assets, const JsonOptions& options) {
    json root;
    root["format"] = kFormatName;
    root["version"] = kFormatVersion;

    root["environment"] = {
        {"skyColor", Vec(scene.environment.skyColor)},
        {"fogDensity", Num(scene.environment.fogDensity)},
    };
    root["light"] = {
        {"direction", Vec(scene.light.direction)},
        {"color", Vec(scene.light.color)},
        {"intensity", Num(scene.light.intensity)},
        {"ambient", Vec(scene.light.ambient)},
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

        if (e->meshRenderer) {
            const MeshRenderer& mr = *e->meshRenderer;
            const auto meshRef = assets.RefOf(mr.mesh.get());
            const auto texRef = assets.RefOf(mr.material.albedo.get());
            if (mr.mesh && !meshRef) log::Warn("Salvar cena: malha de '{}' não está no AssetLibrary", e->name);
            if (mr.material.albedo && !texRef) log::Warn("Salvar cena: textura de '{}' não está no AssetLibrary", e->name);

            je["meshRenderer"] = {
                {"mesh", RefToJson(meshRef, options)},
                {"texture", RefToJson(texRef, options)},
                {"color", Vec(mr.material.color)},
                {"specular", Num(mr.material.specular)},
                {"shininess", Num(mr.material.shininess)},
            };
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
        }
        if (auto it = root.find("light"); it != root.end() && it->is_object()) {
            Read(*it, "direction", out.light.direction);
            Read(*it, "color", out.light.color);
            Read(*it, "intensity", out.light.intensity);
            Read(*it, "ambient", out.light.ambient);
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

            if (auto m = je.find("meshRenderer"); m != je.end() && m->is_object()) {
                MeshRenderer mr;
                std::string problem;
                if (auto ref = m->find("mesh"); ref != m->end() && !ref->is_null()) {
                    mr.mesh = ResolveRef<Mesh>(
                        *ref, [&](std::string_view k, std::string* err) { return assets.GetMesh(k, err); },
                        [&](std::string_view g, std::string* err) { return assets.GetMeshByGuid(g, err); }, problem);
                    if (!mr.mesh) log::Warn("Cena: malha de '{}' não carregada: {}", e.name, problem);
                }
                if (auto ref = m->find("texture"); ref != m->end() && !ref->is_null()) {
                    mr.material.albedo = ResolveRef<Texture2D>(
                        *ref, [&](std::string_view k, std::string* err) { return assets.GetTexture(k, err); },
                        [&](std::string_view g, std::string* err) { return assets.GetTextureByGuid(g, err); },
                        problem);
                    if (!mr.material.albedo) log::Warn("Cena: textura de '{}' não carregada: {}", e.name, problem);
                }
                Read(*m, "color", mr.material.color);
                Read(*m, "specular", mr.material.specular);
                Read(*m, "shininess", mr.material.shininess);
                e.meshRenderer = mr;
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
        return true;
    } catch (const json::exception& e) {
        SetError(error, std::format("JSON inválido: {}", e.what()));
        return false;
    }
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
