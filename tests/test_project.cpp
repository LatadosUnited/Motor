// motor::Project e utilitários de caminho (Paths.h).
#include "TestUtil.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/core/EngineAssets.h>
#include <motor/core/Paths.h>
#include <motor/project/Project.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneSerializer.h>

#include <fstream>

namespace fs = std::filesystem;
using namespace motor;

int main(int argc, char** argv) {
    const fs::path base = TestDir(argc, argv, "test_project_tmp");

    SECTION("ValidateName");
    CHECK(Project::ValidateName("MeuJogo").empty());
    CHECK(Project::ValidateName("Jogo Ação 2").empty());
    CHECK(!Project::ValidateName("").empty());
    CHECK(!Project::ValidateName("a/b").empty());
    CHECK(!Project::ValidateName("nome.").empty());
    CHECK(!Project::ValidateName(" nome").empty());

    SECTION("Create");
    std::string error;
    auto p = Project::Create(base, "Jogo Ação", &error);
    CHECK(p.has_value());
    if (!p) {
        std::printf("  erro: %s\n", error.c_str());
        return TestResult();
    }
    CHECK(fs::is_directory(base / PathFromUtf8("Jogo Ação") / "Assets" / "Scenes"));
    CHECK(fs::is_regular_file(p->ProjectFile()));
    CHECK(ToUtf8(p->ProjectFile().filename()) == "Jogo Ação.mproject");

    SECTION("Create em pasta não vazia falha");
    CHECK(!Project::Create(base, "Jogo Ação", &error).has_value());
    CHECK(!error.empty());

    SECTION("Caminhos relativos");
    const fs::path scene = p->AssetsDir() / "Scenes" / "Principal.mscene";
    CHECK(p->ToRelative(scene) == "Assets/Scenes/Principal.mscene");
    CHECK(p->ToRelative(base / "fora.mscene").empty());
    CHECK(p->ToAbsolute("Assets/Scenes/Principal.mscene") == scene.lexically_normal());

    SECTION("Cena inicial persiste");
    p->SetStartupScene(scene);
    CHECK(p->Save(&error));
    auto byFile = Project::Open(p->ProjectFile(), &error);
    CHECK(byFile.has_value());
    if (byFile) {
        CHECK(byFile->Name() == "Jogo Ação");
        CHECK(byFile->StartupScene() == scene.lexically_normal());
    }
    CHECK(Project::Open(p->RootDir(), &error).has_value());

    SECTION("Camadas de colisão: padrão, salvar e abrir, formato antigo e tabela assimétrica");
    {
        const CollisionLayers defaults;
        CHECK(p->Layers() == defaults && defaults.Label(0) == "Padrão" && defaults.Label(5) == "Camada 5");
        CHECK(defaults.Collide(3, 7) && defaults.Find("Padrão") == 0 && defaults.Find("Nada") == -1);
        CollisionLayers layers;
        layers.names[1] = "Jogador";
        layers.names[3] = "Coleta";
        layers.SetCollide(1, 3, false);
        layers.SetCollide(3, 3, false);
        CHECK(!layers.Collide(3, 1) && layers.Collide(1, 1));  // simétrica
        p->SetLayers(layers);
        CHECK(p->Save(&error));
        auto reopened = Project::Open(p->ProjectFile(), &error);
        CHECK(reopened && reopened->Layers() == layers);

        // Formato 2 (sem "physics"): camadas padrão.
        const fs::path old = base / "Antigo";
        fs::create_directories(old);
        { std::ofstream(old / "Antigo.mproject") << R"({"format": "motor-project", "version": 2, "name": "Antigo"})"; }
        auto antigo = Project::Open(old, &error);
        CHECK(antigo && antigo->Layers() == defaults);
        // Arquivo editado à mão com só um lado dizendo que não: o par não se choca; índice ruim é ignorado.
        { std::ofstream(old / "Antigo.mproject") << R"({"format": "motor-project", "version": 3, "name": "Antigo",
            "physics": {"layers": [{"index": 2, "name": "Água", "collidesWith": [0, 1]}, {"index": 40, "name": "x"}]}})"; }
        antigo = Project::Open(old, &error);
        CHECK(antigo && antigo->Layers().names[2] == "Água" && antigo->Layers().Collide(0, 2));
        CHECK(antigo && !antigo->Layers().Collide(2, 5) && !antigo->Layers().Collide(5, 2) && antigo->Layers().Collide(5, 6));
        // Versão mais nova que a deste editor é recusada.
        { std::ofstream(old / "Antigo.mproject") << R"({"format": "motor-project", "version": 4, "name": "Antigo"})"; }
        CHECK(!Project::Open(old, &error).has_value());
    }

    SECTION("Open inválido");
    std::ofstream(base / "lixo.mproject") << "{ isto não é json";
    CHECK(!Project::Open(base / "lixo.mproject", &error).has_value());
    CHECK(!Project::Open(base / "nao-existe", &error).has_value());

    SECTION("IsInside / RebasePath");
    CHECK(IsInside("C:/a/b/c.txt", "C:/a"));
    CHECK(IsInside("C:/a", "C:/a"));
    CHECK(!IsInside("C:/ab/c.txt", "C:/a"));
    CHECK(!IsInside("", "C:/a"));
    CHECK(RebasePath("C:/a/b/c.txt", "C:/a/b", "C:/a/x") == fs::path("C:/a/x/c.txt").lexically_normal());
    CHECK(RebasePath("C:/a/b", "C:/a/b", "C:/z") == fs::path("C:/z"));
    CHECK(RebasePath("C:/q/c.txt", "C:/a/b", "C:/z") == fs::path("C:/q/c.txt"));

    SECTION("EngineDocsDir");
    CHECK(fs::is_regular_file(EngineDocsDir() / "scripts.md"));
    CHECK(fs::is_regular_file(EngineDocsDir() / "scripts-api.md"));

    SECTION("Salvamento automático e recuperação (.motor/recuperacao)");
    {
        AssetLibrary assets;
        assets.SetRoot(p->RootDir());
        Scene dirtyScene;
        Entity& hero = dirtyScene.CreateEntity("Herói não salvo");
        hero.transform.position = {3.0f, 5.0f, -2.0f};

        CHECK(!SceneSerializer::CheckRecovery(p->RootDir()).has_value());
        CHECK(SceneSerializer::SaveRecovery(p->RootDir(), dirtyScene, assets, scene, "play", &error));

        const auto rec = SceneSerializer::CheckRecovery(p->RootDir());
        CHECK(rec.has_value());
        if (rec) {
            CHECK(rec->originalPath == scene.lexically_normal());
            CHECK(rec->reason == "play");
            CHECK(fs::is_regular_file(rec->sceneFile));
            Scene recovered;
            CHECK(SceneSerializer::Load(rec->sceneFile, recovered, assets, &error));
            const Entity* e = recovered.FindByName("Herói não salvo");
            CHECK(e && e->transform.position == glm::vec3(3.0f, 5.0f, -2.0f));
        }

        SceneSerializer::ClearRecovery(p->RootDir());
        CHECK(!SceneSerializer::CheckRecovery(p->RootDir()).has_value());
    }

    return TestResult();
}
