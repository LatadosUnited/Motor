// Arquivos de recuperação da cena (.motor/recuperacao): salvar, achar e apagar.

#include "motor/scene/SceneSerializer.h"

#include "motor/core/FileWrite.h"
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
constexpr const char* kRecoveryFormat = "motor-recovery";
constexpr int kRecoveryVersion = 1;

std::filesystem::path RecoveryDir(const std::filesystem::path& projectRoot) {
    return projectRoot / ".motor" / "recuperacao";
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

    const auto now = std::chrono::system_clock::now();
    const int64_t savedAt = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::string relScene;
    if (!originalScenePath.empty()) {
        relScene = IsInside(originalScenePath, projectRoot)
                       ? ToGenericUtf8(originalScenePath.lexically_normal().lexically_relative(projectRoot))
                       : ToUtf8(originalScenePath);
    }
    json meta;
    meta["format"] = kRecoveryFormat;
    meta["version"] = kRecoveryVersion;
    meta["scenePath"] = relScene;
    meta["reason"] = std::string(reason);
    meta["savedAt"] = savedAt;

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
    std::ifstream in(metaFile, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    const json meta = json::parse(ss.str(), nullptr, false);
    if (!meta.is_object() || meta.value("format", std::string()) != kRecoveryFormat) return std::nullopt;

    RecoveryInfo info;
    info.sceneFile = sceneFile;
    const std::string savedPath = meta.value("scenePath", std::string());
    if (!savedPath.empty()) {
        const std::filesystem::path p = PathFromUtf8(savedPath);
        info.originalPath = (p.is_absolute() ? p : projectRoot / p).lexically_normal();
    }
    info.reason = meta.value("reason", std::string("autosave"));
    info.savedAt = meta.value("savedAt", int64_t{0});
    return info;
}

void ClearRecovery(const std::filesystem::path& projectRoot) {
    if (projectRoot.empty()) return;
    std::error_code ec;
    std::filesystem::remove_all(RecoveryDir(projectRoot), ec);
}

} // namespace motor::SceneSerializer
