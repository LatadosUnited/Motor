#include "motor/assets/AssetLibrary.h"

#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/project/Project.h"
#include "motor/renderer/Mesh.h"
#include "motor/renderer/Texture.h"

#include <algorithm>
#include <format>

namespace motor {

namespace fs = std::filesystem;

namespace {
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

std::shared_ptr<Texture2D> LoadTexture(const fs::path& file, std::string_view label, std::string* error) {
    ImageData image;
    std::string problem;
    if (!LoadImageFile(file, image, &problem)) {
        SetError(error, std::move(problem));
        return nullptr;
    }
    log::Info("Textura carregada: {} ({}x{})", label, image.width, image.height);
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
    m_database.SetAssetsDir(m_root.empty() ? fs::path{} : m_root / Project::kAssetsFolder);
}

// --- Carregamento -------------------------------------------------------------

std::shared_ptr<Mesh> AssetLibrary::GetMesh(std::string_view key, std::string* error) {
    if (auto builtIn = FindBuiltIn(m_meshes, key)) return builtIn;
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
    auto texture = LoadTexture(file, KeyForFile(file), error);
    if (texture) m_textures.push_back({{}, std::string(guid), texture});
    return texture;
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
        const fs::path file = m_database.PathFor(e.guid);
        return AssetRef{file.empty() ? std::string() : KeyForFile(file), e.guid};
    }
    return std::nullopt;
}

std::optional<AssetRef> AssetLibrary::RefOf(const Mesh* mesh) const {
    return RefIn(m_meshes, mesh);
}

std::optional<AssetRef> AssetLibrary::RefOf(const Texture2D* texture) const {
    return RefIn(m_textures, texture);
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
