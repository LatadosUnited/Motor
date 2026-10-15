#include "motor/assets/AssetDatabase.h"

#include "motor/assets/AssetLoaders.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <random>
#include <sstream>
#include <vector>

namespace motor {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

namespace {
constexpr const char* kMetaFormat = "motor-meta";
constexpr int kMetaVersion = 1;

bool IsHidden(const fs::path& p) {
    const std::string name = ToUtf8(p.filename());
    return !name.empty() && name[0] == '.';
}
} // namespace

fs::path AssetDatabase::MetaPathFor(const fs::path& asset) {
    fs::path meta = asset;
    meta += kMetaExtension;
    return meta;
}

bool AssetDatabase::IsMetaFile(const fs::path& path) {
    return path.extension() == kMetaExtension;
}

bool AssetDatabase::IsTrackedAsset(const fs::path& path) {
    return AssetTypeFromPath(path) != AssetType::Unknown;
}

std::string AssetDatabase::NewGuid() {
    static std::mt19937_64 rng = [] {
        std::random_device rd;
        std::seed_seq seq{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
        return std::mt19937_64(seq);
    }();
    return std::format("{:016x}{:016x}", rng(), rng());
}

bool AssetDatabase::IsValidGuid(std::string_view text) {
    if (text.size() != 32) return false;
    for (char c : text) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

void AssetDatabase::SetAssetsDir(const fs::path& assetsDir) {
    m_assetsDir = assetsDir.empty() ? fs::path{} : assetsDir.lexically_normal();
    m_pathByGuid.clear();
    m_guidByPath.clear();
    m_metaCache.clear();
    Refresh();
}

bool AssetDatabase::WriteMeta(const fs::path& asset, const std::string& guid) {
    json root;
    root["format"] = kMetaFormat;
    root["version"] = kMetaVersion;
    root["guid"] = guid;

    const fs::path meta = MetaPathFor(asset);
    std::ofstream out(meta, std::ios::binary | std::ios::trunc);
    if (!out || !(out << root.dump(2) << '\n') || !out.flush()) {
        log::Warn("Não foi possível escrever {}", ToUtf8(meta));
        return false;
    }
    out.close();
    std::error_code ec;
    m_metaCache[meta] = {fs::last_write_time(meta, ec), guid};
    return true;
}

std::string AssetDatabase::ReadOrCreateMeta(const fs::path& asset) {
    const fs::path meta = MetaPathFor(asset);
    std::error_code ec;
    const auto modified = fs::last_write_time(meta, ec);
    if (!ec) {
        if (auto it = m_metaCache.find(meta); it != m_metaCache.end() && it->second.modified == modified) {
            return it->second.guid;
        }
        std::ifstream in(meta, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        try {
            const json root = json::parse(ss.str());
            const std::string guid = root.value("guid", std::string());
            if (IsValidGuid(guid)) {
                m_metaCache[meta] = {modified, guid};
                return guid;
            }
        } catch (const json::exception&) {
        }
        log::Warn("Arquivo .meta inválido, gerando um novo: {}", ToUtf8(meta));
    }

    std::string guid = NewGuid();
    WriteMeta(asset, guid);
    return guid;
}

void AssetDatabase::Refresh() {
    std::map<std::string, fs::path, std::less<>> pathByGuid;
    std::map<fs::path, std::string> guidByPath;
    std::error_code ec;
    if (m_assetsDir.empty() || !fs::is_directory(m_assetsDir, ec)) {
        m_pathByGuid.clear();
        m_guidByPath.clear();
        return;
    }

    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(m_assetsDir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (IsHidden(it->path())) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && IsTrackedAsset(it->path())) files.push_back(it->path().lexically_normal());
    }
    std::sort(files.begin(), files.end());  // ordem estável para decidir quem fica com um GUID repetido

    for (const fs::path& file : files) {
        std::string guid = ReadOrCreateMeta(file);
        if (auto dup = pathByGuid.find(guid); dup != pathByGuid.end()) {
            // Dois arquivos com o mesmo GUID (cópia feita com o .meta). Fica com ele quem já tinha antes.
            const fs::path other = dup->second;
            const auto previous = m_pathByGuid.find(guid);
            const bool thisIsOriginal = previous != m_pathByGuid.end() && previous->second == file;
            const fs::path& copy = thisIsOriginal ? other : file;
            std::string fresh = NewGuid();
            WriteMeta(copy, fresh);
            log::Info("GUID repetido em {}; a cópia recebeu um GUID novo", ToUtf8(copy.filename()));
            if (thisIsOriginal) {
                guidByPath[other] = fresh;
                pathByGuid[fresh] = other;
                pathByGuid[guid] = file;
            } else {
                guid = std::move(fresh);
                pathByGuid[guid] = file;
            }
            guidByPath[file] = guid;
            continue;
        }
        pathByGuid[guid] = file;
        guidByPath[file] = guid;
    }

    m_pathByGuid = std::move(pathByGuid);
    m_guidByPath = std::move(guidByPath);
}

std::string AssetDatabase::FindGuid(const fs::path& file) const {
    auto it = m_guidByPath.find(file.lexically_normal());
    return it != m_guidByPath.end() ? it->second : std::string();
}

std::string AssetDatabase::GuidFor(const fs::path& file) {
    const fs::path normal = file.lexically_normal();
    if (std::string guid = FindGuid(normal); !guid.empty()) return guid;

    std::error_code ec;
    if (m_assetsDir.empty() || !IsInside(normal, m_assetsDir) || !IsTrackedAsset(normal) ||
        !fs::is_regular_file(normal, ec)) {
        return {};
    }
    // Arquivo novo desde o último Refresh (ex.: acabou de ser importado). Um Refresh completo resolve também
    // o caso de ele ter vindo com um .meta repetido.
    Refresh();
    return FindGuid(normal);
}

fs::path AssetDatabase::PathFor(std::string_view guid) const {
    auto it = m_pathByGuid.find(guid);
    return it != m_pathByGuid.end() ? it->second : fs::path{};
}

} // namespace motor
