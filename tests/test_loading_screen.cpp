// Tela de carregamento (Fase 21) sem janela: a padrão cobre a tela inteira e o texto fica dentro dela, embaixo à
// direita, em qualquer tamanho; a cena de carregamento do projeto vai e volta pelo .mproject; e uma cena de
// carregamento feita no editor (gravada e lida como cena comum) mantém a UI.
#include "TestUtil.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/core/Paths.h>
#include <motor/project/Project.h>
#include <motor/scene/SceneSerializer.h>
#include <motor/ui/LoadingScreen.h>
#include <motor/ui/UiComponents.h>
#include <motor/ui/UiLayout.h>

#include <string>

using namespace motor;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    const fs::path dir = TestDir(argc, argv, "test_loading_screen_tmp");

    SECTION("Tela padrão: fundo na tela inteira, texto embaixo à direita, dentro da tela");
    {
        Scene scene = DefaultLoadingScene("Carregando a fase...");
        const Entity* texto = scene.FindByName("Carregando");
        CHECK(texto && texto->Get<UiText>() && texto->Get<UiText>()->text == "Carregando a fase...");
        CHECK(scene.FindMainCamera() == kNullEntity);  // só UI: sem 3D para desenhar
        for (const glm::vec2 screen : {glm::vec2(1920, 1080), glm::vec2(1280, 720), glm::vec2(800, 600)}) {
            const std::vector<UiNode> layout = ComputeUiLayout(scene, screen);
            CHECK(layout.size() == 2);
            if (layout.size() != 2) continue;
            const UiBox& fundo = layout[0].box;
            CHECK(fundo.min == glm::vec2(0.0f) && fundo.max == screen);
            const UiBox& t = layout[1].box;
            CHECK(t.min.x >= 0.0f && t.min.y >= 0.0f && t.max.x <= screen.x && t.max.y <= screen.y);
            CHECK(t.Center().x > screen.x * 0.5f && t.Center().y > screen.y * 0.75f);  // embaixo à direita
        }
    }

    SECTION("Cena de carregamento do projeto no .mproject");
    {
        std::string error;
        auto project = Project::Create(dir, "Jogo Ação", &error);
        CHECK(project.has_value());
        if (!project) return TestResult();
        CHECK(project->Identity().loadingScene.empty());
        GameIdentity id = project->Identity();
        id.loadingScene = "Assets/Scenes/Carregando.mscene";
        project->SetIdentity(id);
        CHECK(project->Save(&error));
        auto reopened = Project::Open(project->RootDir(), &error);
        CHECK(reopened && reopened->Identity().loadingScene == "Assets/Scenes/Carregando.mscene");

        // A cena feita no editor é uma cena comum: grava e volta com a UI.
        AssetLibrary assets;
        assets.SetRoot(project->RootDir());
        Scene loading = DefaultLoadingScene("Aguarde");
        const fs::path file = project->ToAbsolute(id.loadingScene);
        CHECK(SceneSerializer::Save(file, loading, assets, &error));
        Scene back;
        CHECK(SceneSerializer::Load(file, back, assets, &error));
        const Entity* t = back.FindByName("Carregando");
        CHECK(t && t->Get<UiText>() && t->Get<UiText>()->text == "Aguarde");
        CHECK(ComputeUiLayout(back, {1280, 720}).size() == 2);
    }

    return TestResult();
}
