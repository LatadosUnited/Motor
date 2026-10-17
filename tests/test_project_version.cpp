// Versão da engine (Fase 14): motor/core/Version.h e o campo engineVersion do .mproject (formato 2), com
// projetos do formato 1 abrindo como "versão desconhecida".
#include "TestUtil.h"

#include <motor/core/Paths.h>
#include <motor/core/Version.h>
#include <motor/project/Project.h>

#include <nlohmann/json.hpp>

#include <format>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace motor;

namespace {
nlohmann::json ReadJson(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return nlohmann::json::parse(ss.str());
}

void WriteText(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}
} // namespace

int main(int argc, char** argv) {
    const fs::path base = TestDir(argc, argv, "test_project_version_tmp");

    SECTION("Version.h");
    CHECK(std::string(kVersion) == std::format("{}.{}.{}", kVersionMajor, kVersionMinor, kVersionPatch));
    CHECK(kVersionMajor > 0 || kVersionMinor >= 2);  // a Fase 14 começou na 0.2.0

    SECTION("Projeto novo grava a versão da engine (formato 2)");
    std::string error;
    auto created = Project::Create(base, "Jogo Ação", &error);
    CHECK(created.has_value());
    if (!created) {
        std::printf("  erro: %s\n", error.c_str());
        return TestResult();
    }
    CHECK(created->EngineVersion() == kVersion);
    const nlohmann::json saved = ReadJson(created->ProjectFile());
    CHECK(saved.value("version", 0) == 2);
    CHECK(saved.value("engineVersion", std::string()) == kVersion);
    CHECK(!fs::exists(fs::path(created->ProjectFile()) += ".tmp"));  // gravou o .tmp e renomeou

    auto reopened = Project::Open(created->RootDir(), &error);
    CHECK(reopened.has_value() && reopened->EngineVersion() == kVersion);

    SECTION("Formato 1 abre com versão desconhecida e sobe para 2 ao salvar");
    const fs::path old = base / PathFromUtf8("Antigo Ação");
    fs::create_directories(old / "Assets");
    WriteText(old / PathFromUtf8("Antigo Ação.mproject"),
              R"({"format": "motor-project", "version": 1, "name": "Antigo Ação", "startupScene": "Assets/a.mscene"})");
    auto v1 = Project::Open(old, &error);
    CHECK(v1.has_value());
    if (v1) {
        CHECK(v1->EngineVersion().empty());
        CHECK(v1->Name() == "Antigo Ação");
        CHECK(v1->Save(&error));
        CHECK(v1->EngineVersion() == kVersion);
        const nlohmann::json upgraded = ReadJson(v1->ProjectFile());
        CHECK(upgraded.value("version", 0) == 2);
        CHECK(upgraded.value("engineVersion", std::string()) == kVersion);
        CHECK(upgraded.value("startupScene", std::string()) == "Assets/a.mscene");
    }

    SECTION("Projeto de outra versão: lê a versão gravada");
    const fs::path other = base / "Outro";
    fs::create_directories(other);
    WriteText(other / "Outro.mproject",
              R"({"format": "motor-project", "version": 2, "name": "Outro", "engineVersion": "0.9.1"})");
    auto v2 = Project::Open(other, &error);
    CHECK(v2.has_value() && v2->EngineVersion() == "0.9.1");

    SECTION("Formato futuro é recusado");
    WriteText(other / "Outro.mproject", R"({"format": "motor-project", "version": 3, "name": "Outro"})");
    CHECK(!Project::Open(other, &error).has_value());

    return TestResult();
}
