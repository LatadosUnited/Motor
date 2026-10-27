// Arquivos de recuperação da cena (.motor/recuperacao): salvar, achar e apagar.

#include "motor/scene/SceneSerializer.h"

#include "motor/common/FileWrite.h"
#include "SceneJson.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/scene/Scene.h"
#include "motor/scene/Terrain.h"

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
constexpr const char* kRecoveryFormat = "motor-recovery";
constexpr int kRecoveryVersion = 1;

std::filesystem::path RecoveryDir(const std::filesystem::path& projectRoot) {
    return projectRoot / ".motor" / "recuperacao";
}

// Caminho guardado no recuperacao.json: relativo ao projeto quando está dentro dele.
std::string StoredPath(const std::filesystem::path& path, const std::filesystem::path& projectRoot) {
    if (path.empty()) return {};
    return IsInside(path, projectRoot) ? ToGenericUtf8(path.lexically_normal().lexically_relative(projectRoot))
                                       : ToUtf8(path);
}

std::filesystem::path ResolveStored(const std::string& stored, const std::filesystem::path& projectRoot) {
    if (stored.empty()) return {};
    const std::filesystem::path p = PathFromUtf8(stored);
    return (p.is_absolute() ? p : projectRoot / p).lexically_normal();
}

json ReadMeta(const std::filesystem::path& projectRoot) {
    std::ifstream in(RecoveryDir(projectRoot) / "recuperacao.json", std::ios::binary);
    if (!in) return json();
    std::stringstream ss;
    ss << in.rdbuf();
    json meta = json::parse(ss.str(), nullptr, false);
    if (!meta.is_object() || meta.value("format", std::string()) != kRecoveryFormat) return json();
    return meta;
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
    if (!WriteFileAtomic(sceneFile, text, error)) return false;

    // Alturas não salvas dos terrenos (Fase 28): de cada um, só os blocos mudados (TerrainData::SaveChanges).
    const std::filesystem::path terrainsDir = dir / "terrenos";
    std::filesystem::remove_all(terrainsDir, ec);
    json terrains = json::array();
    std::vector<const TerrainData*> done;  // duas entidades com o mesmo arquivo: uma vez
    for (const EntityId id : scene.EntitiesWith<Terrain>()) {
        const Entity* e = scene.Find(id);
        const Terrain* terrain = e ? e->Get<Terrain>() : nullptr;
        if (!terrain || terrain->terrain.file.empty()) continue;
        const TerrainData* data = terrain->terrain.Data();
        if (!data || !data->IsDirty() || std::find(done.begin(), done.end(), data) != done.end()) continue;
        done.push_back(data);
        const std::string name = std::to_string(terrains.size());
        if (!data->SaveChanges(terrainsDir / name, error)) return false;
        terrains.push_back(
            {{"file", StoredPath(terrain->terrain.file, projectRoot)}, {"changes", "terrenos/" + name}});
    }

    const auto now = std::chrono::system_clock::now();
    const int64_t savedAt = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    const std::string relScene = StoredPath(originalScenePath, projectRoot);
    json meta;
    meta["format"] = kRecoveryFormat;
    meta["version"] = kRecoveryVersion;
    meta["scenePath"] = relScene;
    meta["reason"] = std::string(reason);
    meta["savedAt"] = savedAt;
    meta["terrains"] = std::move(terrains);

    const std::filesystem::path metaFile = dir / "recuperacao.json";
    if (!WriteFileAtomic(metaFile, meta.dump(2) + '\n', error)) return false;
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
    const json meta = ReadMeta(projectRoot);
    if (meta.is_null()) return std::nullopt;

    RecoveryInfo info;
    info.sceneFile = sceneFile;
    info.originalPath = ResolveStored(meta.value("scenePath", std::string()), projectRoot);
    info.reason = meta.value("reason", std::string("autosave"));
    info.savedAt = meta.value("savedAt", int64_t{0});
    return info;
}

int ApplyRecoveryTerrains(const std::filesystem::path& projectRoot, const Scene& scene, std::string* error) {
    const json meta = ReadMeta(projectRoot);
    const auto list = meta.is_object() ? meta.find("terrains") : meta.end();
    if (meta.is_null() || list == meta.end() || !list->is_array()) return 0;
    int applied = 0;
    for (const json& entry : *list) {
        if (!entry.is_object()) continue;
        const std::filesystem::path file = ResolveStored(entry.value("file", std::string()), projectRoot);
        const std::filesystem::path folder =
            RecoveryDir(projectRoot) / PathFromUtf8(entry.value("changes", std::string()));
        if (file.empty()) continue;
        for (const EntityId id : scene.EntitiesWith<Terrain>()) {
            const Entity* e = scene.Find(id);
            const Terrain* terrain = e ? e->Get<Terrain>() : nullptr;
            if (!terrain || terrain->terrain.file.empty() || terrain->terrain.file.lexically_normal() != file) continue;
            TerrainData* data = terrain->terrain.Data();
            if (!data) continue;
            std::string failure;
            if (!data->ApplyChanges(folder, &failure) && error) *error += (error->empty() ? "" : "; ") + failure;
            ++applied;
            break;  // as outras entidades com o mesmo arquivo dividem o mesmo TerrainData
        }
    }
    return applied;
}

void ClearRecovery(const std::filesystem::path& projectRoot) {
    if (projectRoot.empty()) return;
    std::error_code ec;
    std::filesystem::remove_all(RecoveryDir(projectRoot), ec);
}

} // namespace motor::SceneSerializer
