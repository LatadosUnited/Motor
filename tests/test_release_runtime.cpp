// Motor em Release para a exportação (Fase 21) sem compilar nada: qual pasta serve (Player, DLLs de runtime e um
// SDK de scripts em Release), o motivo de cada uma que não serve e as pastas da DLL de scripts de Release,
// separadas das do editor.
#include "TestUtil.h"

#include "export/GameExport.h"
#include "export/ReleaseRuntime.h"

#include <motor/core/Paths.h>

#include <fstream>
#include <string>

using namespace editor;
using namespace motor;
namespace fs = std::filesystem;

namespace {

void Write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

// Pasta de bin falsa: Player, DLLs e um MotorSDK.json com esse buildType ("" = sem SDK).
void FakeBin(const fs::path& dir, const std::string& buildType, bool player = true) {
    if (player) Write(dir / "Player.exe", "MZ");
    for (const char* dll : GameExport::kRuntimeDlls) Write(dir / dll, "MZ");
    if (buildType.empty()) return;
    Write(dir / "sdk" / "MotorSDK.json",
          R"({"format": "motor-sdk", "version": 1, "cmake": "C:/cmake.exe", "generator": "Ninja",
              "makeProgram": "C:/ninja.exe", "cxxCompiler": "C:/c++.exe", "buildType": ")" + buildType +
              R"(", "sdkCMake": "MotorSDK.cmake"})");
}

} // namespace

int main(int argc, char** argv) {
    const fs::path dir = TestDir(argc, argv, "test_release_runtime_tmp");

    const fs::path semPlayer = dir / "sem player";
    const fs::path debug = dir / "debug";
    const fs::path semSdk = dir / "sem sdk";
    const fs::path release = dir / PathFromUtf8("Versões") / "release";
    FakeBin(semPlayer, "Release", false);
    FakeBin(debug, "Debug");
    FakeBin(semSdk, "");
    FakeBin(release, "Release");

    SECTION("A primeira pasta que serve");
    {
        std::string why;
        auto found = ReleaseRuntime::Find({semPlayer, debug, semSdk, release, dir / "nao existe"}, &why);
        CHECK(found.has_value());
        if (found) {
            CHECK(found->dir == release);
            CHECK(found->toolchain.buildType == "Release");
            CHECK(found->toolchain.sdkCMake == release / "sdk" / "MotorSDK.cmake");  // relativo ao .json
        }
    }

    SECTION("Nenhuma serve: o motivo de cada uma");
    {
        std::string why;
        CHECK(!ReleaseRuntime::Find({semPlayer, debug, semSdk}, &why).has_value());
        CHECK(why.find("sem Player.exe") != std::string::npos);
        CHECK(why.find("compilado em Debug") != std::string::npos);
        CHECK(why.find("sem SDK de scripts") != std::string::npos);
        fs::remove(release / "libMotor.dll");
        CHECK(!ReleaseRuntime::Find({release}, &why).has_value());
        CHECK(why.find("sem libMotor.dll") != std::string::npos);
    }

    SECTION("Candidatas: a pasta do editor, release/ ao lado e a do build de Release");
    {
        const auto list = ReleaseRuntime::DefaultCandidates();
        CHECK(list.size() >= 2);
        if (list.size() >= 2) CHECK(list[1] == list[0] / "release");
    }

    SECTION("DLL de scripts de Release em pastas próprias");
    {
        const fs::path project = dir / PathFromUtf8("Jogo Ação");
        const ScriptProject::Paths editorPaths = ScriptProject::PathsFor(project);
        const ScriptProject::Paths releasePaths = ReleaseRuntime::ScriptPaths(project);
        CHECK(releasePaths.scriptsDir == editorPaths.scriptsDir);
        CHECK(releasePaths.binaryDir != editorPaths.binaryDir && releasePaths.cmakeDir != editorPaths.cmakeDir);
        CHECK(releasePaths.dll.parent_path() == releasePaths.binaryDir);
        CHECK(releasePaths.dll.filename() == "GameScripts.dll");
    }

    return TestResult();
}
