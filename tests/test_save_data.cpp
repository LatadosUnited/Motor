// Progresso salvo (Fase 21) sem janela: valores de cada tipo (e int/float entre si), arquivo de save numa pasta com
// acento, arquivo estragado guardado como .corrompido, slots (nome limpo, existe, apagar, listar, gravar os que
// mudaram) e a API dos scripts: um "jogo" grava, fecha e o seguinte continua de onde parou; e o Play do editor.
#include "TestUtil.h"

#include "play/PlayHost.h"
#include "play/PlayMode.h"

#include <motor/core/Paths.h>
#include <motor/project/SaveData.h>
#include <motor/scene/GameHost.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>

#include <fstream>
#include <sstream>
#include <string>

using namespace motor;
namespace fs = std::filesystem;

namespace {

std::string Read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct HostDeTeste final : GameHost {
    SaveStore saves;
    explicit HostDeTeste(fs::path folder) : saves(std::move(folder)) {}
    GameOptions Options() const override { return {}; }
    void ApplyOptions(const GameOptions&) override {}
    std::vector<Resolution> Resolutions() const override { return {}; }
    std::string GameVersion() const override { return "1.0"; }
    void QuitGame() override {}
    SaveStore& Saves() override { return saves; }
};

// Menu principal com "Continuar": na primeira vez não há save; joga, passa de fase e grava.
struct Progresso : Script {
    bool tinhaSave = false;
    int faseLida = 0;
    void OnStart() override {
        tinhaSave = HasSave();
        faseLida = SaveSlot().GetInt("fase", 1);
    }
    void OnUpdate(float) override {
        SaveData& save = SaveSlot();
        save.SetInt("fase", faseLida + 1);
        save.SetVec3("posicao", {1.0f, 2.0f, 3.0f});
        SaveSlot("config").SetBool("legendas", true);  // não gravado aqui: o host grava ao fechar
        save.Save();
    }
};

} // namespace

int main(int argc, char** argv) {
    const fs::path dir = TestDir(argc, argv, "test_save_data_tmp") / PathFromUtf8("Jogo Ação") / "saves";

    SECTION("Valores, tipos e conversões");
    {
        SaveData s;
        CHECK(s.Empty() && !s.IsDirty());
        s.SetInt("vidas", 3);
        s.SetFloat("tempo", 12.5f);
        s.SetBool("chefe", true);
        s.SetString("nome", "Ação");
        s.SetVec3("posicao", {1, 2, 3});
        CHECK(s.IsDirty());
        CHECK(s.GetInt("vidas") == 3 && s.GetFloat("vidas") == 3.0f);
        CHECK(s.GetFloat("tempo") == 12.5f && s.GetInt("tempo") == 12);
        CHECK(s.GetBool("chefe") && s.GetString("nome") == "Ação");
        CHECK(s.GetVec3("posicao") == glm::vec3(1, 2, 3));
        CHECK(s.GetInt("nome", -1) == -1);           // tipo errado: o padrão
        CHECK(s.GetString("falta", "x") == "x");
        CHECK(s.Keys() == (std::vector<std::string>{"chefe", "nome", "posicao", "tempo", "vidas"}));
        s.Remove("chefe");
        CHECK(!s.Has("chefe"));
    }

    SECTION("JSON e arquivo");
    {
        SaveData s(dir / "teste.json");
        s.SetInt("grande", 2000000000);
        s.SetFloat("inteiro", 2.0f);  // continua float depois de ler
        s.SetString("texto", "linha \"com\" aspas");
        s.SetVec3("v", {-1.5f, 0, 9});
        std::string error;
        CHECK(s.Save(&error));
        CHECK(!s.IsDirty());
        SaveData back(dir / "teste.json");
        CHECK(back.Load(&error));
        CHECK(back.GetInt("grande") == 2000000000);
        CHECK(back.GetFloat("inteiro") == 2.0f && back.GetString("texto") == "linha \"com\" aspas");
        CHECK(back.GetVec3("v") == glm::vec3(-1.5f, 0, 9));
        CHECK(Read(dir / "teste.json").find("\"motor-save\"") != std::string::npos);

        // Sem mudança, Set não suja; sem arquivo, Save falha com mensagem.
        back.SetInt("grande", 2000000000);
        CHECK(!back.IsDirty());
        SaveData semArquivo;
        CHECK(!semArquivo.Save(&error) && !error.empty());
    }

    SECTION("Arquivo estragado vira .corrompido");
    {
        std::ofstream(dir / "ruim.json") << "{ isto não é json";
        SaveData s(dir / "ruim.json");
        std::string error;
        CHECK(!s.Load(&error));
        CHECK(s.Empty() && error.find("corrompido") != std::string::npos);
        CHECK(fs::exists(dir / "ruim.json.corrompido"));
        CHECK(!SaveData().FromJson(R"({"format": "motor-save", "version": 99})"));  // versão futura
    }

    SECTION("Slots");
    {
        CHECK(SaveStore::SlotFileName("Slot 1: Ação/x") == "Slot 1_ Ação_x.json");
        CHECK(SaveStore::SlotFileName("") == "progresso.json");
        SaveStore store(dir);
        SaveData& a = store.Slot("Slot 1");
        CHECK(&a == &store.Slot("Slot 1"));  // o mesmo objeto
        CHECK(!store.Exists("Slot 1"));
        a.SetInt("x", 1);
        CHECK(store.Exists("Slot 1"));  // valores ainda não gravados contam
        store.Slot("outro");             // vazio e limpo: não grava
        CHECK(store.SaveAll());
        CHECK(fs::exists(dir / "Slot 1.json") && !fs::exists(dir / "outro.json"));
        const auto list = store.List();
        CHECK(std::find(list.begin(), list.end(), "Slot 1") != list.end());
        CHECK(store.Delete("Slot 1"));
        CHECK(!fs::exists(dir / "Slot 1.json") && !store.Exists("Slot 1"));
        CHECK(a.Empty() && !a.IsDirty());  // a referência continua valendo, vazia
    }

    auto& registry = ComponentRegistry::Instance();
    registry.Register<Progresso>("Progresso", "Progresso");

    SECTION("Scripts: um jogo grava, o próximo continua");
    const fs::path gameDir = dir.parent_path() / "jogo";
    for (int run = 0; run < 2; ++run) {
        HostDeTeste host(gameDir);
        Scene scene;
        scene.CreateEntity("Menu").Add<Progresso>();
        SceneRuntime runtime(scene);
        runtime.SetHost(&host);
        runtime.Start();
        runtime.Update(0.016f);
        const Progresso* p = scene.FindByName("Menu")->Get<Progresso>();
        CHECK(p->tinhaSave == (run == 1));
        CHECK(p->faseLida == run + 1);
        runtime.Stop();
        CHECK(host.saves.Slot("config").IsDirty() == (run == 0));  // na segunda vez o valor já era esse
        CHECK(host.saves.SaveAll());  // o que o Player faz ao fechar
    }
    {
        SaveData config(gameDir / "config.json");
        CHECK(config.Load() && config.GetBool("legendas"));
        SaveData progresso(gameDir / "progresso.json");
        CHECK(progresso.Load() && progresso.GetInt("fase") == 3 && progresso.GetVec3("posicao") == glm::vec3(1, 2, 3));
    }

    SECTION("Scripts sem host: na memória, sem quebrar");
    {
        Scene scene;
        scene.CreateEntity("Menu").Add<Progresso>();
        SceneRuntime runtime(scene);
        runtime.Start();
        runtime.Update(0.016f);
        runtime.Stop();
        CHECK(scene.FindByName("Menu")->Get<Progresso>()->faseLida >= 1);
    }

    SECTION("Play do editor: pasta própria, relida a cada Play, gravada ao parar");
    {
        const fs::path playDir = dir.parent_path() / ".motor" / "saves";
        editor::PlayHost host;
        editor::PlayMode play;
        play.SetHost(&host);
        for (int run = 0; run < 2; ++run) {
            Scene scene;
            scene.CreateEntity("Menu").Add<Progresso>();
            host.Reset({}, "1.0", playDir);
            play.Play(scene, kNullEntity);
            play.Update(0.016f);
            CHECK(scene.FindByName("Menu")->Get<Progresso>()->faseLida == run + 1);
            EntityId selected = kNullEntity;
            play.Stop(scene, selected);
            CHECK(host.Saves().SaveAll());
        }
        CHECK(fs::exists(playDir / "progresso.json") && fs::exists(playDir / "config.json"));
    }

    registry.Unregister(registry.Find("Progresso"));
    return TestResult();
}
