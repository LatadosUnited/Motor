// Assets do projeto: materiais (.mmat), ler, gravar e recarregar quando o arquivo muda.

#include "motor/assets/AssetLibrary.h"

#include "AssetLibraryUtil.h"
#include "motor/common/FileRead.h"
#include "motor/common/FileWrite.h"

#include "motor/assets/Model.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/project/Project.h"
#include "motor/renderer/Mesh.h"
#include "motor/renderer/Texture.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

namespace motor {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;
using namespace asset_library;

namespace {
constexpr const char* kMaterialFormat = "motor-material";
// 1: Blinn-Phong (cor, textura, especular, brilho). 2: PBR da Fase 7 (metal, rugosidade, mapas, emissão).
constexpr int kMaterialVersion = 3;  // 3: modo de alfa, limite do recorte e duas faces (Fase 18)

// float -> double pela representação decimal mais curta ("0.6" no arquivo, e não "0.6000000238418579").
double Num(float value) {
    return std::stod(std::format("{}", value));
}

// Escreve num .tmp e troca no fim, para não deixar o arquivo pela metade se algo falhar.
bool WriteTextAtomic(const fs::path& file, const std::string& text, std::string* error) {
    return WriteFileAtomic(file, text, error);
}
} // namespace
// --- Materiais --------------------------------------------------------------------

std::shared_ptr<Material> AssetLibrary::GetMaterial(std::string_view key, std::string* error) {
    const std::string guid = m_database.GuidFor(FileForKey(key));
    if (guid.empty()) {
        SetError(error, std::format("Material '{}' não existe no projeto.", key));
        return nullptr;
    }
    return GetMaterialByGuid(guid, error);
}

std::shared_ptr<Material> AssetLibrary::GetMaterialByGuid(std::string_view guid, std::string* error) {
    if (auto cached = FindByGuid(m_materials, guid)) return cached;
    fs::path file = m_database.PathFor(guid);
    if (file.empty()) {
        m_database.Refresh();
        file = m_database.PathFor(guid);
    }
    if (file.empty()) {
        SetError(error, std::format("Nenhum arquivo do projeto tem o GUID {}.", guid));
        return nullptr;
    }
    const std::optional<std::string> text = ReadTextFile(file);
    if (!text) {
        SetError(error, std::format("Não foi possível abrir '{}'.", ToUtf8(file)));
        return nullptr;
    }
    auto material = std::make_shared<Material>();
    std::string problem;
    if (!MaterialFromJson(*text, *material, &problem)) {
        SetError(error, std::format("{}: {}", KeyForFile(file), problem));
        return nullptr;
    }
    std::error_code ec;
    m_materialTimes[std::string(guid)] = fs::last_write_time(file, ec);
    m_materials.push_back({{}, std::string(guid), material});
    log::Info("Material carregado: {}", KeyForFile(file));
    return material;
}

MaterialTextureRefs AssetLibrary::TextureRefsOf(const Material& m) const {
    auto ref = [&](const std::shared_ptr<Texture2D>& texture) -> std::optional<AssetRef> {
        if (!texture) return std::nullopt;
        auto r = RefOf(texture.get());
        if (!r) log::Warn("Salvar material: uma textura não está no AssetLibrary");
        return r;
    };
    return {ref(m.albedo), ref(m.normalMap), ref(m.metallicRoughnessMap), ref(m.occlusionMap), ref(m.emissiveMap)};
}

bool AssetLibrary::WriteMaterialFile(const fs::path& file, const Material& material, std::string* error) {
    return WriteMaterialFile(file, material, TextureRefsOf(material), error);
}

bool AssetLibrary::WriteMaterialFile(const fs::path& file, const Material& material,
                                     const MaterialTextureRefs& textures, std::string* error) {
    if (!WriteTextAtomic(file, MaterialToJson(material, textures), error)) return false;
    // Material em cache: a data nova é a da nossa gravação, não precisa reler.
    if (const std::string guid = m_database.GuidFor(file); !guid.empty() && m_materialTimes.contains(guid)) {
        std::error_code ec;
        m_materialTimes[guid] = fs::last_write_time(file, ec);
    }
    return true;
}

bool AssetLibrary::SaveMaterial(const Material* material, std::string* error) {
    for (const auto& e : m_materials) {
        if (e.asset.get() != material) continue;
        const fs::path file = m_database.PathFor(e.guid);
        if (file.empty()) {
            SetError(error, std::format("O arquivo do material (GUID {}) não existe mais.", e.guid));
            return false;
        }
        return WriteMaterialFile(file, *material, error);
    }
    SetError(error, "O material não veio de um arquivo do projeto.");
    return false;
}

size_t AssetLibrary::ReloadChangedMaterials() {
    size_t count = 0;
    for (const auto& e : m_materials) {
        const fs::path file = m_database.PathFor(e.guid);
        if (file.empty()) continue;
        std::error_code ec;
        const auto modified = fs::last_write_time(file, ec);
        if (ec || m_materialTimes[e.guid] == modified) continue;
        m_materialTimes[e.guid] = modified;
        std::string problem;
        Material reloaded;
        const std::optional<std::string> text = ReadTextFile(file);
        if (!text || !MaterialFromJson(*text, reloaded, &problem)) {
            log::Warn("Material {} não foi relido: {}", KeyForFile(file), problem);
            continue;
        }
        *e.asset = std::move(reloaded);  // mesmo objeto: todos que o usam veem a mudança
        log::Info("Material relido: {}", KeyForFile(file));
        ++count;
    }
    return count;
}

std::string AssetLibrary::MaterialToJson(const Material& m) const {
    return MaterialToJson(m, TextureRefsOf(m));
}

namespace {
// Textura embutida pelo nome ("Xadrez"); de arquivo, { "guid", "path" }; nenhuma, null.
json TextureJson(const std::optional<AssetRef>& ref) {
    if (!ref) return nullptr;
    if (ref->guid.empty()) return ref->name;
    json texture = {{"guid", ref->guid}};
    if (!ref->name.empty()) texture["path"] = ref->name;
    return texture;
}

void ReadNumber(const json& root, const char* key, float& out) {
    if (auto it = root.find(key); it != root.end() && it->is_number()) out = it->get<float>();
}

bool AllNumbers(const json& array, size_t size) {
    return array.is_array() && array.size() == size &&
           std::all_of(array.begin(), array.end(), [](const json& v) { return v.is_number(); });
}
} // namespace

std::string AssetLibrary::MaterialToJson(const Material& m, const MaterialTextureRefs& t) const {
    json root;
    root["format"] = kMaterialFormat;
    root["version"] = kMaterialVersion;
    root["color"] = json::array({Num(m.color.r), Num(m.color.g), Num(m.color.b), Num(m.color.a)});
    root["texture"] = TextureJson(t.albedo);
    root["metallic"] = Num(m.metallic);
    root["roughness"] = Num(m.roughness);
    root["specular"] = Num(m.specular);
    root["normalMap"] = TextureJson(t.normal);
    root["normalStrength"] = Num(m.normalStrength);
    root["metallicRoughnessMap"] = TextureJson(t.metallicRoughness);
    root["occlusionMap"] = TextureJson(t.occlusion);
    root["occlusionStrength"] = Num(m.occlusionStrength);
    root["emissive"] = json::array({Num(m.emissive.r), Num(m.emissive.g), Num(m.emissive.b)});
    root["emissiveIntensity"] = Num(m.emissiveIntensity);
    root["emissiveMap"] = TextureJson(t.emissive);
    root["blend"] = BlendModeName(m.blend);
    root["alphaCutoff"] = Num(m.alphaCutoff);
    root["doubleSided"] = m.doubleSided;

    // Uma chave por linha, cada valor compacto (a cor fica [1.0,0.5,0.2,1.0]).
    std::string text = "{\n";
    bool first = true;
    for (const auto& [key, value] : root.items()) {
        if (!first) text += ",\n";
        text += std::format("  {}: {}", json(key).dump(), value.dump(-1, ' ', false, json::error_handler_t::replace));
        first = false;
    }
    return text + "\n}\n";
}

namespace {
std::shared_ptr<Texture2D> TextureFromJson(AssetLibrary& assets, const json& value, const char* what) {
    std::string problem;
    std::shared_ptr<Texture2D> texture;
    if (value.is_string()) {
        texture = assets.GetTexture(value.get<std::string>(), &problem);
    } else if (value.is_object()) {
        const std::string guid = value.value("guid", std::string());
        const std::string path = value.value("path", std::string());
        if (!guid.empty()) texture = assets.GetTextureByGuid(guid, &problem);
        if (!texture && !path.empty()) texture = assets.GetTexture(path, nullptr);  // .meta perdido: pelo caminho
    } else {
        return nullptr;  // null ou formato errado: sem textura
    }
    if (!texture) log::Warn("Material: {} não carregado: {}", what, problem);
    return texture;
}
} // namespace

bool AssetLibrary::MaterialFromJson(std::string_view text, Material& out, std::string* error) {
    const json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object() || root.value("format", std::string()) != kMaterialFormat) {
        SetError(error, "O arquivo não é um material do Motor.");
        return false;
    }
    const int version = root.value("version", 0);
    if (version < 1 || version > kMaterialVersion) {
        SetError(error, std::format("Versão de material não suportada: {} (esperado até {}).", version, kMaterialVersion));
        return false;
    }

    // Leitura tolerante: campo ausente ou com formato errado fica com o valor padrão.
    Material m;
    if (auto it = root.find("color"); it != root.end() && AllNumbers(*it, 4)) {
        for (glm::length_t i = 0; i < 4; ++i) m.color[i] = (*it)[static_cast<size_t>(i)].get<float>();
    }
    ReadNumber(root, "metallic", m.metallic);
    ReadNumber(root, "roughness", m.roughness);
    ReadNumber(root, "specular", m.specular);
    ReadNumber(root, "normalStrength", m.normalStrength);
    ReadNumber(root, "occlusionStrength", m.occlusionStrength);
    ReadNumber(root, "emissiveIntensity", m.emissiveIntensity);
    ReadNumber(root, "alphaCutoff", m.alphaCutoff);
    if (auto it = root.find("blend"); it != root.end() && it->is_string()) {
        for (BlendMode mode : {BlendMode::Opaque, BlendMode::Cutout, BlendMode::Transparent}) {
            if (it->get<std::string>() == BlendModeName(mode)) m.blend = mode;
        }
    }
    if (auto it = root.find("doubleSided"); it != root.end() && it->is_boolean()) m.doubleSided = it->get<bool>();
    if (auto it = root.find("emissive"); it != root.end() && AllNumbers(*it, 3)) {
        for (glm::length_t i = 0; i < 3; ++i) m.emissive[i] = (*it)[static_cast<size_t>(i)].get<float>();
    }
    // Versão 1 (Blinn-Phong): o brilho vira rugosidade.
    if (version < 2) {
        float shininess = 32.0f;
        ReadNumber(root, "shininess", shininess);
        m.roughness = RoughnessFromShininess(shininess);
    }
    if (auto it = root.find("texture"); it != root.end()) m.albedo = TextureFromJson(*this, *it, "textura");
    if (auto it = root.find("normalMap"); it != root.end()) m.normalMap = TextureFromJson(*this, *it, "mapa de normal");
    if (auto it = root.find("metallicRoughnessMap"); it != root.end()) {
        m.metallicRoughnessMap = TextureFromJson(*this, *it, "mapa metal/rugosidade");
    }
    if (auto it = root.find("occlusionMap"); it != root.end()) m.occlusionMap = TextureFromJson(*this, *it, "mapa de oclusão");
    if (auto it = root.find("emissiveMap"); it != root.end()) m.emissiveMap = TextureFromJson(*this, *it, "mapa de emissão");
    out = std::move(m);
    return true;
}

} // namespace motor
