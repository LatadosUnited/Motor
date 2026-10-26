#include "export/ReleaseRuntime.h"

#include "export/GameExport.h"

#include <motor/core/EngineAssets.h>
#include <motor/core/Paths.h>

#include <format>

namespace editor::ReleaseRuntime {

namespace fs = std::filesystem;

std::vector<fs::path> DefaultCandidates() {
    std::vector<fs::path> list = {motor::ExecutableDir(), motor::ExecutableDir() / "release"};
#ifdef MOTOR_RELEASE_BIN_DIR
    list.push_back(motor::PathFromUtf8(MOTOR_RELEASE_BIN_DIR));
#endif
    return list;
}

std::optional<Runtime> Find(const std::vector<fs::path>& candidates, std::string* why) {
    std::string reasons;
    for (const fs::path& dir : candidates) {
        std::error_code ec;
        std::string problem;
        if (!fs::is_regular_file(dir / "Player.exe", ec)) {
            problem = "sem Player.exe";
        } else {
            for (const char* dll : GameExport::kRuntimeDlls) {
                if (!fs::is_regular_file(dir / dll, ec)) {
                    problem = std::format("sem {}", dll);
                    break;
                }
            }
        }
        std::optional<ScriptProject::Toolchain> toolchain;
        if (problem.empty()) {
            std::string error;
            toolchain = ScriptProject::LoadToolchain(dir / "sdk" / "MotorSDK.json", &error);
            if (!toolchain) problem = "sem SDK de scripts";
            else if (toolchain->buildType != "Release") problem = std::format("compilado em {}", toolchain->buildType);
        }
        if (problem.empty()) return Runtime{dir, *toolchain};
        reasons += std::format("{}{}: {}", reasons.empty() ? "" : "; ", motor::ToUtf8(dir), problem);
    }
    if (why) *why = reasons;
    return std::nullopt;
}

ScriptProject::Paths ScriptPaths(const fs::path& projectRoot) {
    ScriptProject::Paths p = ScriptProject::PathsFor(projectRoot);
    p.cmakeDir = p.workDir / "cmake-release";
    p.binaryDir = p.workDir / "build-release";
    p.dll = p.binaryDir / (std::string(ScriptProject::kTargetName) + ".dll");
    return p;
}

} // namespace editor::ReleaseRuntime
