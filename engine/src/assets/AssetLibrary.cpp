#include "motor/assets/AssetLibrary.h"

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

namespace {
constexpr const char* kMaterialFormat = "motor-material";
constexpr int kMaterialVersion = 1;

// float -> double pela representação decimal mais curta ("0.6" no arquivo, e não "0.6000000238418579").
double Num(float value) {
    return std::stod(std::format("{}", value));
}

bool ReadText(const fs::path& file, std::string& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

template <class T>
std::shared_ptr<T> FindBuiltIn(const std::vector<AssetLibrary::Entry<T>>& list, std::string_view name) {
    for (const auto& e : list) {
        if (e.guid.empty() && e.name == name) return e.asset;
    }
    return nullptr;
}

template <class T>
std::shared_ptr<T> FindByGuid(const std::vector<AssetLibrary::Entry<T>>& list, std::string_view guid) {
    for (const auto& e : list) {
        if (!e.guid.empty() && e.guid == guid) return e.asset;
    }
    return nullptr;
}

void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

// "arquivo#parte" -> ("arquivo", "parte"); sem '#', a parte fica vazia.
std::pair<std::string_view, std::string_view> SplitSubAsset(std::string_view text) {
    const size_t hash = text.find(AssetLibrary::kSubAssetSeparator);
    if (hash == std::string_view::npos) return {text, {}};
    return {text.substr(0, hash), text.substr(hash + 1)};
}

std::shared_ptr<Mesh> LoadMesh(const fs::path& file, std::string_view label, std::string* error) {
    MeshData data;
    std::string problem;
    if (!LoadObj(file, data, &problem)) {
        SetError(error, std::move(problem));
        return nullptr;
    }
    log::Info("Malha carregada: {} ({} vértices, {} triângulos)", label, data.vertices.size(), data.indices.size() / 3);
    return std::make_shared<Mesh>(data.vertices, data.indices);
}

// Escreve num .tmp e renomeia no fim, para não deixar o arquivo pela metade se algo falhar.
bool WriteTextAtomic(const fs::path& file, const std::string& text, std::string* error) {
    fs::path temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out || !(out << text) || !out.flush()) {
            SetError(error, std::format("Não foi possível escrever '{}'.", ToUtf8(temp)));
            return false;
        }
    }
    std::error_code ec;
    fs::rename(temp, file, ec);
    if (ec) {
        fs::remove(file, ec);  // algumas implementações não substituem o destino
        fs::rename(temp, file, ec);
    }
    if (ec) {
        SetError(error, std::format("Não foi possível salvar '{}': {}", ToUtf8(file), ec.message()));
        return false;
    }
    return true;
}

std::shared_ptr<Texture2D> LoadTexture(const fs::path& file, std::string_view label,
                                       const AssetLibrary::TextureFactory& factory, std::string* error) {
    ImageData image;
    std::string problem;
    if (!LoadImageFile(file, image, &problem)) {
        SetError(error, std::move(problem));
        return nullptr;
    }
    log::Info("Textura carregada: {} ({}x{})", label, image.width, image.height);
    if (factory) return factory(image);
    return std::make_shared<Texture2D>(image.width, image.height, image.pixels.data());
}
} // namespace

void AssetLibrary::AddMesh(std::string name, std::shared_ptr<Mesh> mesh) {
    m_meshes.push_back({std::move(name), {}, std::move(mesh)});
}

void AssetLibrary::AddTexture(std::string name, std::shared_ptr<Texture2D> texture) {
    m_textures.push_back({std::move(name), {}, std::move(texture)});
}

void AssetLibrary::SetRoot(const fs::path& root) {
    m_root = root.empty() ? fs::path{} : root.lexically_normal();
    std::erase_if(m_meshes, [](const auto& e) { return !e.guid.empty(); });
    std::erase_if(m_textures, [](const auto& e) { return !e.guid.empty(); });
    m_materials.clear();
    m_materialTimes.clear();
    m_models.clear();
    m_modelTimes.clear();
    m_clips.clear();
    m_database.SetAssetsDir(m_root.empty() ? fs::path{} : m_root / Project::kAssetsFolder);
}

// --- Carregamento -------------------------------------------------------------

std::shared_ptr<Mesh> AssetLibrary::GetMesh(std::string_view key, std::string* error) {
    if (auto builtIn = FindBuiltIn(m_meshes, key)) return builtIn;
    if (const auto [file, part] = SplitSubAsset(key); !part.empty()) {
        const auto model = GetModel(file, error);
        if (!model) return nullptr;
        if (auto mesh = model->FindMesh(part)) return mesh;
        SetError(error, std::format("O modelo '{}' não tem a malha '{}'.", file, part));
        return nullptr;
    }
    const std::string guid = m_database.GuidFor(FileForKey(key));
    if (guid.empty()) {
        SetError(error, std::format("Malha '{}' não existe no projeto.", key));
        return nullptr;
    }
    return GetMeshByGuid(guid, error);
}

std::shared_ptr<Texture2D> AssetLibrary::GetTexture(std::string_view key, std::string* error) {
    if (auto builtIn = FindBuiltIn(m_textures, key)) return builtIn;
    const std::string guid = m_database.GuidFor(FileForKey(key));
    if (guid.empty()) {
        SetError(error, std::format("Textura '{}' não existe no projeto.", key));
        return nullptr;
    }
    return GetTextureByGuid(guid, error);
}

std::shared_ptr<Mesh> AssetLibrary::GetMeshByGuid(std::string_view guid, std::string* error) {
    if (auto cached = FindByGuid(m_meshes, guid)) return cached;
    if (const auto [fileGuid, part] = SplitSubAsset(guid); !part.empty()) {
        const auto model = GetModelByGuid(fileGuid, error);
        if (!model) return nullptr;
        if (auto mesh = model->FindMesh(part)) return mesh;
        SetError(error, std::format("O modelo (GUID {}) não tem a malha '{}'.", fileGuid, part));
        return nullptr;
    }
    fs::path file = m_database.PathFor(guid);
    if (file.empty()) {
        m_database.Refresh();  // pode ser um arquivo que chegou depois da última varredura
        file = m_database.PathFor(guid);
    }
    if (file.empty()) {
        SetError(error, std::format("Nenhum arquivo do projeto tem o GUID {}.", guid));
        return nullptr;
    }
    auto mesh = LoadMesh(file, KeyForFile(file), error);
    if (mesh) m_meshes.push_back({{}, std::string(guid), mesh});
    return mesh;
}

std::shared_ptr<Texture2D> AssetLibrary::GetTextureByGuid(std::string_view guid, std::string* error) {
    if (auto cached = FindByGuid(m_textures, guid)) return cached;
    fs::path file = m_database.PathFor(guid);
    if (file.empty()) {
        m_database.Refresh();
        file = m_database.PathFor(guid);
    }
    if (file.empty()) {
        SetError(error, std::format("Nenhum arquivo do projeto tem o GUID {}.", guid));
        return nullptr;
    }
    auto texture = LoadTexture(file, KeyForFile(file), m_textureFactory, error);
    if (texture) m_textures.push_back({{}, std::string(guid), texture});
    return texture;
}

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
    std::string text;
    if (!ReadText(file, text)) {
        SetError(error, std::format("Não foi possível abrir '{}'.", ToUtf8(file)));
        return nullptr;
    }
    auto material = std::make_shared<Material>();
    std::string problem;
    if (!MaterialFromJson(text, *material, &problem)) {
        SetError(error, std::format("{}: {}", KeyForFile(file), problem));
        return nullptr;
    }
    std::error_code ec;
    m_materialTimes[std::string(guid)] = fs::last_write_time(file, ec);
    m_materials.push_back({{}, std::string(guid), material});
    log::Info("Material carregado: {}", KeyForFile(file));
    return material;
}

bool AssetLibrary::WriteMaterialFile(const fs::path& file, const Material& material, std::string* error) {
    return WriteMaterialFile(file, material, RefOf(material.albedo.get()), error);
}

bool AssetLibrary::WriteMaterialFile(const fs::path& file, const Material& material,
                                     const std::optional<AssetRef>& texture, std::string* error) {
    if (!WriteTextAtomic(file, MaterialToJson(material, texture), error)) return false;
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
        std::string text, problem;
        Material reloaded;
        if (!ReadText(file, text) || !MaterialFromJson(text, reloaded, &problem)) {
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
    return MaterialToJson(m, RefOf(m.albedo.get()));
}

std::string AssetLibrary::MaterialToJson(const Material& m, const std::optional<AssetRef>& textureRef) const {
    json root;
    root["format"] = kMaterialFormat;
    root["version"] = kMaterialVersion;
    root["color"] = json::array({Num(m.color.r), Num(m.color.g), Num(m.color.b), Num(m.color.a)});
    json texture = nullptr;
    if (const auto& ref = textureRef) {
        if (ref->guid.empty()) {
            texture = ref->name;
        } else {
            texture = {{"guid", ref->guid}};
            if (!ref->name.empty()) texture["path"] = ref->name;
        }
    } else if (m.albedo) {
        log::Warn("Salvar material: a textura não está no AssetLibrary");
    }
    root["texture"] = std::move(texture);
    root["specular"] = Num(m.specular);
    root["shininess"] = Num(m.shininess);

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
    if (auto it = root.find("color"); it != root.end() && it->is_array() && it->size() == 4) {
        const bool numbers = std::all_of(it->begin(), it->end(), [](const json& v) { return v.is_number(); });
        if (numbers) {
            for (glm::length_t i = 0; i < 4; ++i) m.color[i] = (*it)[static_cast<size_t>(i)].get<float>();
        }
    }
    if (auto it = root.find("specular"); it != root.end() && it->is_number()) m.specular = it->get<float>();
    if (auto it = root.find("shininess"); it != root.end() && it->is_number()) m.shininess = it->get<float>();
    if (auto it = root.find("texture"); it != root.end() && !it->is_null()) {
        std::string problem;
        if (it->is_string()) {
            m.albedo = GetTexture(it->get<std::string>(), &problem);
        } else if (it->is_object()) {
            const std::string guid = it->value("guid", std::string());
            const std::string path = it->value("path", std::string());
            if (!guid.empty()) m.albedo = GetTextureByGuid(guid, &problem);
            if (!m.albedo && !path.empty()) m.albedo = GetTexture(path, nullptr);  // .meta perdido: pelo caminho
        }
        if (!m.albedo) log::Warn("Material: textura não carregada: {}", problem);
    }
    out = std::move(m);
    return true;
}

// --- Modelos (glTF) -------------------------------------------------------------

std::shared_ptr<Model> AssetLibrary::GetModel(std::string_view key, std::string* error) {
    const fs::path file = FileForKey(key);
    const std::string guid = AssetTypeFromPath(file) == AssetType::Model ? m_database.GuidFor(file) : std::string();
    if (guid.empty()) {
        SetError(error, std::format("Modelo '{}' não existe no projeto.", key));
        return nullptr;
    }
    return GetModelByGuid(guid, error);
}

std::shared_ptr<Model> AssetLibrary::GetModelByGuid(std::string_view guid, std::string* error) {
    fs::path file = m_database.PathFor(guid);
    std::error_code ec;
    if (auto cached = FindByGuid(m_models, guid)) {
        // Arquivo mudou no disco (exportado de novo): relê. Quem já usa as malhas antigas continua com elas.
        const auto it = m_modelTimes.find(guid);
        if (file.empty() || it == m_modelTimes.end() || fs::last_write_time(file, ec) == it->second || ec) return cached;
        const std::string prefix = std::string(guid) + kSubAssetSeparator;
        std::erase_if(m_models, [&](const auto& e) { return e.guid == guid; });
        std::erase_if(m_meshes, [&](const auto& e) { return e.guid.starts_with(prefix); });
        std::erase_if(m_clips, [&](const auto& e) { return e.guid.starts_with(prefix); });
    }
    if (file.empty()) {
        m_database.Refresh();
        file = m_database.PathFor(guid);
    }
    if (file.empty() || AssetTypeFromPath(file) != AssetType::Model) {
        SetError(error, std::format("Nenhum modelo do projeto tem o GUID {}.", guid));
        return nullptr;
    }
    ModelData data;
    std::string problem;
    if (!LoadGltf(file, data, &problem)) {
        SetError(error, std::move(problem));
        return nullptr;
    }
    auto model = BuildModel(std::move(data));
    // Malhas e clipes entram no cache com o GUID "<arquivo>#<nome>": RefOf e as cenas os acham como os outros.
    for (const auto& m : model->meshes) m_meshes.push_back({{}, std::format("{}#{}", guid, m.name), m.mesh});
    for (const auto& c : model->clips) m_clips.push_back({{}, std::format("{}#{}", guid, c->name), c});
    m_models.push_back({{}, std::string(guid), model});
    m_modelTimes[std::string(guid)] = fs::last_write_time(file, ec);
    log::Info("Modelo carregado: {} ({} malhas, {} nós, {} clipes)", KeyForFile(file), model->meshes.size(),
              model->skeleton->joints.size(), model->clips.size());
    return model;
}

std::shared_ptr<AnimationClip> AssetLibrary::GetClip(std::string_view key, std::string* error) {
    const auto [file, part] = SplitSubAsset(key);
    const auto model = GetModel(file, error);
    if (!model) return nullptr;
    if (auto clip = model->FindClip(part)) return clip;
    SetError(error, std::format("O modelo '{}' não tem o clipe '{}'.", file, part));
    return nullptr;
}

std::shared_ptr<AnimationClip> AssetLibrary::GetClipByGuid(std::string_view guid, std::string* error) {
    if (auto cached = FindByGuid(m_clips, guid)) return cached;
    const auto [fileGuid, part] = SplitSubAsset(guid);
    const auto model = GetModelByGuid(fileGuid, error);
    if (!model) return nullptr;
    if (auto clip = model->FindClip(part)) return clip;
    SetError(error, std::format("O modelo (GUID {}) não tem o clipe '{}'.", fileGuid, part));
    return nullptr;
}

std::shared_ptr<Mesh> AssetLibrary::FindMesh(std::string_view name) const {
    return FindBuiltIn(m_meshes, name);
}

std::shared_ptr<Texture2D> AssetLibrary::FindTexture(std::string_view name) const {
    return FindBuiltIn(m_textures, name);
}

// --- Referências ----------------------------------------------------------------

template <class T>
std::optional<AssetRef> AssetLibrary::RefIn(const std::vector<Entry<T>>& list, const T* asset) const {
    if (!asset) return std::nullopt;
    for (const auto& e : list) {
        if (e.asset.get() != asset) continue;
        if (e.guid.empty()) return AssetRef{e.name, {}};
        // O caminho é sempre o atual: se o arquivo foi movido, o banco já sabe onde ele está.
        const auto [fileGuid, part] = SplitSubAsset(e.guid);
        const fs::path file = m_database.PathFor(fileGuid);
        std::string key = file.empty() ? std::string() : KeyForFile(file);
        if (!key.empty() && !part.empty()) key = std::format("{}#{}", key, part);
        return AssetRef{std::move(key), e.guid};
    }
    return std::nullopt;
}

std::optional<AssetRef> AssetLibrary::RefOf(const Mesh* mesh) const {
    return RefIn(m_meshes, mesh);
}

std::optional<AssetRef> AssetLibrary::RefOf(const Texture2D* texture) const {
    return RefIn(m_textures, texture);
}

std::optional<AssetRef> AssetLibrary::RefOf(const Material* material) const {
    return RefIn(m_materials, material);
}

std::optional<AssetRef> AssetLibrary::RefOf(const Model* model) const {
    return RefIn(m_models, model);
}

std::optional<AssetRef> AssetLibrary::RefOf(const AnimationClip* clip) const {
    return RefIn(m_clips, clip);
}

// --- Caminhos -------------------------------------------------------------------

std::string AssetLibrary::KeyForFile(const fs::path& file) const {
    if (m_root.empty() || !IsInside(file, m_root)) return {};
    const fs::path rel = file.lexically_normal().lexically_relative(m_root);
    if (rel.empty() || rel == ".") return {};
    return ToGenericUtf8(rel);
}

fs::path AssetLibrary::FileForKey(std::string_view key) const {
    if (m_root.empty() || !IsFileKey(key)) return {};
    const fs::path file = (m_root / PathFromUtf8(key)).lexically_normal();
    return IsInside(file, m_root) ? file : fs::path{};  // recusa "../" saindo do projeto
}

std::vector<std::string> AssetLibrary::ScanFiles(AssetType type) const {
    std::vector<std::string> keys;
    std::error_code ec;
    if (m_root.empty() || !fs::is_directory(m_root, ec)) return keys;

    for (auto it = fs::recursive_directory_iterator(m_root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const std::string name = ToUtf8(it->path().filename());
        if (!name.empty() && name[0] == '.') {  // pastas/arquivos ocultos (.git, etc.)
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && AssetTypeFromPath(it->path()) == type) keys.push_back(KeyForFile(it->path()));
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

} // namespace motor
