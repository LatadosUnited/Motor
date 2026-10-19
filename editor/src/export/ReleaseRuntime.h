#pragma once

// Motor em Release para a exportação (Fase 21): o jogo exportado leva o Player, a libMotor.dll e a DLL de scripts
// compilados em Release (rápidos), mesmo quando o editor é de Debug. Uma pasta serve se tiver o Player.exe, as
// DLLs de runtime (GameExport::kRuntimeDlls) e um SDK de scripts em Release (sdk/MotorSDK.json com buildType
// "Release"), contra o qual a DLL de scripts é recompilada ao exportar.
//
// Pastas procuradas, nesta ordem:
//   1. a do editor (o editor de Release já serve);
//   2. <pasta do editor>/release/ (a instalação do Motor, alvo motor_package, traz uma);
//   3. MOTOR_RELEASE_BIN_DIR, gravada no build (padrão: <código>/build-release/bin, para quem compila o Motor).

#include "scripts/ScriptProject.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace editor::ReleaseRuntime {

struct Runtime {
    std::filesystem::path dir;           // Player.exe e as DLLs
    ScriptProject::Toolchain toolchain;  // SDK em Release (buildType "Release")
};

std::vector<std::filesystem::path> DefaultCandidates();

// Primeira pasta que serve; sem nenhuma, nullopt e `why` com o motivo de cada candidata (para o aviso).
std::optional<Runtime> Find(const std::vector<std::filesystem::path>& candidates, std::string* why = nullptr);

// Onde a DLL de scripts de Release é compilada: .motor/scripts/cmake-release e build-release, separadas das do
// editor (que continuam em Debug, recarregadas no Play).
ScriptProject::Paths ScriptPaths(const std::filesystem::path& projectRoot);

} // namespace editor::ReleaseRuntime
