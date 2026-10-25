// Assets do projeto: embutidos e de arquivo (malhas, texturas, céus, modelos, clipes) por nome ou GUID.
// Materiais (.mmat) em AssetLibraryMaterials.cpp.

#include "motor/assets/AssetLibrary.h"

#include "AssetLibraryUtil.h"

#include "motor/assets/Model.h"
#include "motor/core/JobSystem.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/project/Project.h"
#include "motor/renderer/Mesh.h"
#include "motor/renderer/Texture.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <functional>
#include <fstream>
#include <sstream>

namespace motor {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;
using namespace asset_library;

namespace {
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
    m_environmentMaps.clear();
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

size_t AssetLibrary::PreloadTextures(std::span<const std::string> guids) {
    // Texturas que faltam: as pedidas direto e as dos materiais pedidos (os GUIDs que aparecem no .mmat).
    std::vector<std::pair<std::string, fs::path>> pending;
    auto addTexture = [&](const std::string& guid) {
        if (FindByGuid(m_textures, guid)) return;
        for (const auto& p : pending) {
            if (p.first == guid) return;
        }
        fs::path file = m_database.PathFor(guid);
        if (!file.empty() && AssetTypeFromPath(file) == AssetType::Texture) pending.push_back({guid, std::move(file)});
    };
    std::vector<std::string> inMaterial;
    for (const std::string& guid : guids) {
        const fs::path file = m_database.PathFor(guid);
        const AssetType type = file.empty() ? AssetType::Unknown : AssetTypeFromPath(file);
        if (type == AssetType::Texture) {
            addTexture(guid);
        } else if (type == AssetType::Material) {
            std::ifstream in(file, std::ios::binary);
            const json root = json::parse(in, nullptr, false);
            if (root.is_discarded()) continue;
            inMaterial.clear();
            const std::function<void(const json&)> walk = [&](const json& j) {
                if (j.is_string()) {
                    if (const auto& s = j.get_ref<const std::string&>(); AssetDatabase::LooksLikeGuid(s)) inMaterial.push_back(s);
                } else if (j.is_structured()) {
                    for (const json& item : j) walk(item);
                }
            };
            walk(root);
            for (const std::string& texture : inMaterial) addTexture(texture);
        }
    }
    if (pending.empty()) return 0;

    // Ler e decodificar (PNG/JPG) é o que custa: em paralelo. A textura OpenGL só nesta thread (a do contexto).
    std::vector<ImageData> images(pending.size());
    std::vector<uint8_t> ok(pending.size(), 0);
    JobSystem::Get().ParallelFor(pending.size(), 1, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) ok[i] = LoadImageFile(pending[i].second, images[i], nullptr) ? 1 : 0;
    });
    size_t loaded = 0;
    for (size_t i = 0; i < pending.size(); ++i) {
        if (!ok[i]) continue;  // quem pedir pelo GetTextureByGuid tenta de novo e recebe o erro
        const ImageData& image = images[i];
        log::Info("Textura carregada: {} ({}x{})", KeyForFile(pending[i].second), image.width, image.height);
        auto texture = m_textureFactory ? m_textureFactory(image)
                                        : std::make_shared<Texture2D>(image.width, image.height, image.pixels.data());
        if (!texture) continue;
        m_textures.push_back({{}, pending[i].first, std::move(texture)});
        ++loaded;
    }
    return loaded;
}

// --- Céus HDR ----------------------------------------------------------------------

std::shared_ptr<const HdrImage> AssetLibrary::GetEnvironmentMap(std::string_view key, std::string* error) {
    const fs::path file = FileForKey(key);
    const std::string guid = AssetTypeFromPath(file) == AssetType::EnvironmentMap ? m_database.GuidFor(file) : std::string();
    if (guid.empty()) {
        SetError(error, std::format("Céu HDR '{}' não existe no projeto.", key));
        return nullptr;
    }
    return GetEnvironmentMapByGuid(guid, error);
}

std::shared_ptr<const HdrImage> AssetLibrary::GetEnvironmentMapByGuid(std::string_view guid, std::string* error) {
    if (auto cached = FindByGuid(m_environmentMaps, guid)) return cached;
    fs::path file = m_database.PathFor(guid);
    if (file.empty()) {
        m_database.Refresh();
        file = m_database.PathFor(guid);
    }
    if (file.empty() || AssetTypeFromPath(file) != AssetType::EnvironmentMap) {
        SetError(error, std::format("Nenhum céu HDR do projeto tem o GUID {}.", guid));
        return nullptr;
    }
    auto image = std::make_shared<HdrImage>();
    if (!LoadHdrFile(file, *image, error)) return nullptr;
    log::Info("Céu HDR carregado: {} ({}x{})", KeyForFile(file), image->width, image->height);
    std::shared_ptr<const HdrImage> loaded = std::move(image);
    m_environmentMaps.push_back({{}, std::string(guid), loaded});
    return loaded;
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
    if (!LoadModelFile(file, data, &problem)) {
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

std::optional<AssetRef> AssetLibrary::RefOf(const HdrImage* image) const {
    return RefIn<const HdrImage>(m_environmentMaps, image);
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
