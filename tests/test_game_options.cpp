// Opções do jogador (Fase 21) sem janela: valores corrigidos, JSON (campos que faltam ficam no padrão), arquivo
// opcoes.json numa pasta com acento, opções padrão no .mproject (e projeto antigo sem elas), pasta de dados do
// jogador e a API dos scripts (GetOptions/ApplyOptions/QuitGame) com um GameHost de teste e com o do editor.
#include "TestUtil.h"

#include "play/PlayHost.h"
#include "play/PlayMode.h"

#include <motor/core/Paths.h>
#include <motor/project/GameOptions.h>
#include <motor/project/Project.h>
#include <motor/scene/GameHost.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>

#include <fstream>
#include <string>

using namespace motor;
namespace fs = std::filesystem;

namespace {

// Host de teste: guarda o que os scripts pediram.
struct HostDeTeste final : GameHost {
    GameOptions options;
    int applied = 0;
    bool quit = false;
    GameOptions Options() const override { return options; }
    void ApplyOptions(const GameOptions& o) override {
        options = o;
        ++applied;
    }
    std::vector<Resolution> Resolutions() const override { return {{1280, 720}, {1920, 1080}}; }
    std::string GameVersion() const override { return "1.2.0"; }
    void QuitGame() override { quit = true; }
};

// Menu de opções de mentira: no OnStart lê, troca e aplica; no primeiro OnUpdate pede para sair.
struct MenuOpcoes : Script {
    bool viuPadrao = false;
    std::string versao;
    int resolucoes = 0;
    void OnStart() override {
        GameOptions o = GetOptions();
        viuPadrao = !o.fullscreen && o.msaa == 4;
        resolucoes = static_cast<int>(Resolutions().size());
        versao = GameVersion();
        o.fullscreen = true;
        o.msaa = 3;  // vira 2 ou 4 (Clamp): a API corrige antes de passar adiante
        o.shadows = ShadowSetting::Low;
        ApplyOptions(o);
    }
    void OnUpdate(float) override { QuitGame(); }
};

} // namespace

int main(int argc, char** argv) {
    const fs::path dir = TestDir(argc, argv, "test_game_options_tmp");

    SECTION("Clamp");
    {
        GameOptions o;
        o.msaa = 3;
        o.Clamp();
        CHECK(o.msaa == 2 || o.msaa == 4);
        o.msaa = 100;
        o.Clamp();
        CHECK(o.msaa == 8);
        o.msaa = 0;
        o.Clamp();
        CHECK(o.msaa == 1);
        o.width = 800;
        o.height = 0;  // só um dos dois: volta ao padrão
        o.Clamp();
        CHECK(o.width == 0 && o.height == 0);
        o.width = -5;
        o.height = 600;
        o.Clamp();
        CHECK(o.width == 0 && o.height == 0);
        o.shadows = static_cast<ShadowSetting>(9);
        o.Clamp();
        CHECK(o.shadows == ShadowSetting::High);
    }

    SECTION("JSON: ida e volta, campos que faltam e texto inválido");
    {
        GameOptions o;
        o.fullscreen = true;
        o.width = 1920;
        o.height = 1080;
        o.vsync = false;
        o.msaa = 8;
        o.shadows = ShadowSetting::Medium;
        GameOptions back;
        CHECK(GameOptionsFromJson(GameOptionsToJson(o), back));
        CHECK(back == o);
        CHECK(GameOptionsToJson(o).find("\"medias\"") != std::string::npos);

        GameOptions defaults;
        defaults.fullscreen = true;
        defaults.msaa = 2;
        GameOptions partial;
        CHECK(GameOptionsFromJson(R"({"vsync": false, "shadows": "desligadas"})", partial, defaults));
        CHECK(partial.fullscreen && partial.msaa == 2 && !partial.vsync && partial.shadows == ShadowSetting::Off);
        CHECK(GameOptionsFromJson(R"({"shadows": "ultra"})", partial, defaults));  // nome desconhecido: padrão
        CHECK(partial.shadows == defaults.shadows);

        std::string error;
        GameOptions bad;
        bad.msaa = 1;
        CHECK(!GameOptionsFromJson("{ não é json", bad, defaults, &error));
        CHECK(!error.empty() && bad == defaults);
    }

    SECTION("opcoes.json numa pasta com acento");
    {
        const fs::path file = dir / PathFromUtf8("Jogo Ação") / "opcoes.json";
        GameOptions o;
        o.width = 1600;
        o.height = 900;
        o.shadows = ShadowSetting::Low;
        std::string error;
        CHECK(SaveGameOptions(file, o, &error));
        GameOptions read;
        CHECK(LoadGameOptions(file, read, {}, &error));
        CHECK(read == o);
        GameOptions defaults;
        defaults.vsync = false;
        CHECK(!LoadGameOptions(dir / "nao-existe.json", read, defaults));
        CHECK(read == defaults);
    }

    SECTION("Pasta de dados do jogador");
    {
        const fs::path data = PlayerDataDir("Jogo Ação: 2/3");
        CHECK(!data.empty());
        CHECK(ToUtf8(data.filename()) == "Jogo Ação_ 2_3");
        CHECK(ToUtf8(PlayerDataDir("  ...").filename()) == "Jogo");
    }

    SECTION("Opções padrão no .mproject");
    {
        std::string error;
        auto project = Project::Create(dir, "Projeto Opções", &error);
        CHECK(project.has_value());
        if (!project) return TestResult();
        CHECK(project->DefaultOptions() == GameOptions{});
        GameOptions o;
        o.fullscreen = true;
        o.msaa = 2;
        project->SetDefaultOptions(o);
        CHECK(project->Save(&error));
        auto reopened = Project::Open(project->RootDir(), &error);
        CHECK(reopened && reopened->DefaultOptions() == o);

        // Projeto do formato 3 (antes da Fase 21): abre com as opções padrão.
        const fs::path old = dir / "Antigo";
        fs::create_directories(old);
        std::ofstream(old / "Antigo.mproject") << R"({"format": "motor-project", "version": 3, "name": "Antigo"})";
        auto antigo = Project::Open(old, &error);
        CHECK(antigo && antigo->DefaultOptions() == GameOptions{});
    }

    auto& registry = ComponentRegistry::Instance();
    registry.Register<MenuOpcoes>("MenuOpcoes", "Menu de opções");

    SECTION("Scripts: sem host, as opções são as padrão e nada quebra");
    {
        Scene scene;
        scene.CreateEntity("Menu").Add<MenuOpcoes>();
        SceneRuntime runtime(scene);
        runtime.Start();
        runtime.Update(0.016f);
        CHECK(scene.FindByName("Menu")->Get<MenuOpcoes>()->viuPadrao);
        CHECK(scene.FindByName("Menu")->Get<MenuOpcoes>()->resolucoes == 0);
        runtime.Stop();
    }

    SECTION("Scripts: com host, aplicam (corrigido) e pedem para sair");
    {
        Scene scene;
        scene.CreateEntity("Menu").Add<MenuOpcoes>();
        HostDeTeste host;
        SceneRuntime runtime(scene);
        runtime.SetHost(&host);
        runtime.Start();
        CHECK(host.applied == 1);
        CHECK(host.options.fullscreen && host.options.shadows == ShadowSetting::Low);
        CHECK(host.options.msaa == 2 || host.options.msaa == 4);
        CHECK(scene.FindByName("Menu")->Get<MenuOpcoes>()->resolucoes == 2);
        CHECK(scene.FindByName("Menu")->Get<MenuOpcoes>()->versao == "1.2.0");
        CHECK(!host.quit);
        runtime.Update(0.016f);
        CHECK(host.quit);
        runtime.Stop();
    }

    SECTION("Play do editor: começa nas padrão do projeto, guarda o que o jogo aplicou, QuitGame");
    {
        Scene scene;
        scene.CreateEntity("Menu").Add<MenuOpcoes>();
        editor::PlayHost host;
        GameOptions defaults;
        defaults.msaa = 8;
        host.Reset(defaults, "3.0");
        editor::PlayMode play;
        play.SetHost(&host);
        play.Play(scene, kNullEntity);
        CHECK(host.Options().fullscreen && host.Options().shadows == ShadowSetting::Low);
        CHECK(scene.FindByName("Menu")->Get<MenuOpcoes>()->versao == "3.0");
        CHECK(!host.TakeQuit());
        play.Update(0.016f);
        CHECK(host.TakeQuit());
        CHECK(!host.TakeQuit());  // o pedido é consumido
        EntityId selected = kNullEntity;
        play.Stop(scene, selected);
        host.Reset(defaults);  // próximo Play: volta às padrão
        CHECK(host.Options() == defaults);
    }

    registry.Unregister(registry.Find("MenuOpcoes"));
    return TestResult();
}
