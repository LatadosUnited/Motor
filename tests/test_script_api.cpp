// API dos scripts da Fase 17, sem DLL: temporizadores, mensagens, números aleatórios, GetComponent, campos
// EntityRef (inclusive ao duplicar e em prefabs), registro curto (MOTOR_FIELDS, rótulo tirado do nome), campo
// renomeado (formerKey) e os avisos de valores perdidos ao religar componentes.
// Sem janela nem OpenGL.
#include "TestUtil.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/core/Paths.h>
#include <motor/scene/Components.h>
#include <motor/scene/Prefab.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/scene/SceneSerializer.h>
#include <motor/scene/ScriptModule.h>

#include <format>
#include <set>
#include <string>
#include <vector>

using namespace motor;
namespace fs = std::filesystem;

namespace {
std::vector<std::string> g_log;

// Temporizadores: um After e um Every; o Every para sozinho depois de 3 vezes.
struct Cronometro : Script {
    int disparos = 0;
    bool acabou = false;
    TimerId repetido = 0;
    void OnStart() override {
        After(0.5f, [this] { acabou = true; });
        repetido = Every(0.2f, [this] {
            if (++disparos == 3) CancelTimer(repetido);
        });
    }
};

// Mensagens: o Botao manda "abrir" para a Porta (EntityRef) e "alarme" para todos.
struct Porta : Script {
    bool aberta = false;
    float ultimoValor = 0.0f;
    void OnMessage(const Message& m) override {
        g_log.push_back(std::string("porta:") + std::string(m.text));
        if (m.text == "abrir") {
            aberta = true;
            ultimoValor = m.value;
        }
    }
};

struct Botao : Script {
    EntityRef porta;
    void OnStart() override {
        Send(porta, "abrir", 2.5f);
        Broadcast("alarme");
        // GetComponent: da outra entidade (pela referência) e desta.
        if (Porta* p = GetComponent<Porta>(porta)) g_log.push_back(p->aberta ? "botao:viu-aberta" : "botao:viu-fechada");
        if (GetComponent<Botao>() == this) g_log.push_back("botao:achou-a-si");
        if (!GetComponent<Porta>()) g_log.push_back("botao:sem-porta-aqui");
        if (!GetEntity(EntityRef{})) g_log.push_back("botao:ref-vazia");
    }
    void OnMessage(const Message& m) override { g_log.push_back(std::string("botao:") + std::string(m.text)); }
};

// Números aleatórios.
struct Dado : Script {
    std::set<int> faces;
    bool foraDoIntervalo = false;
    std::vector<float> sequencia;
    void OnStart() override {
        for (int i = 0; i < 600; ++i) {
            const int face = RandomInt(1, 6);
            faces.insert(face);
            const float x = Random(-2.0f, 3.0f);
            if (face < 1 || face > 6 || x < -2.0f || x > 3.0f) foraDoIntervalo = true;
        }
        SetRandomSeed(42);
        for (int i = 0; i < 5; ++i) sequencia.push_back(Random(0.0f, 1.0f));
        if (Chance(0.0f) || !Chance(1.0f)) foraDoIntervalo = true;
    }
};

// Registro curto.
struct Pulo : Script {
    float alturaMaxima = 2.0f;
    int vidas = 3;
    EntityRef alvo;
};
MOTOR_FIELDS(Pulo, alturaMaxima, vidas, alvo)

// Campo renomeado: "forca" era "velocidade".
struct Empurrar : Script {
    float forca = 1.0f;
    int modo = 0;
};

void Register() {
    auto& registry = ComponentRegistry::Instance();
    registry.Register<Cronometro>("Cronometro", "Cronômetro");
    registry.Register<Porta>("Porta", "Porta");
    registry.Register<Botao>("Botao", "Botão").Field("porta", &Botao::porta);
    registry.Register<Dado>("Dado", "Dado");
    detail::RegisterModuleScripts(registry);  // MOTOR_FIELDS deste arquivo
    registry.Register<Empurrar>("Empurrar", "Empurrar")
        .Field("forca", "Força", &Empurrar::forca, {.formerKey = "velocidade"})
        .Field("modo", &Empurrar::modo);
}

template <class T>
T& Comp(Scene& scene, EntityId id) {
    return *scene.Find(id)->Get<T>();
}

bool Logged(const std::string& line) {
    for (const std::string& l : g_log) {
        if (l == line) return true;
    }
    return false;
}

const EntityRef& RefOf(Scene& scene, EntityId id) {
    return scene.Find(id)->Get<Pulo>()->alvo;
}
} // namespace

int main(int argc, char** argv) {
    Register();
    const auto& registry = ComponentRegistry::Instance();

    SECTION("Temporizadores: After uma vez, Every até cancelar, no tempo do jogo");
    {
        Scene scene;
        Entity& e = scene.CreateEntity("Relogio");
        e.Add<Cronometro>();
        const EntityId id = e.id;
        SceneRuntime runtime(scene);
        runtime.Start();
        CHECK(runtime.TimerCount() == 2);
        for (int i = 0; i < 4; ++i) runtime.Update(0.1f);  // 0.4 s
        CHECK(Comp<Cronometro>(scene, id).disparos == 2 && !Comp<Cronometro>(scene, id).acabou);
        for (int i = 0; i < 6; ++i) runtime.Update(0.1f);  // 1.0 s
        CHECK(Comp<Cronometro>(scene, id).acabou);
        CHECK(Comp<Cronometro>(scene, id).disparos == 3);  // cancelado no 3º
        CHECK(runtime.TimerCount() == 0);

        // Entidade destruída: os temporizadores dela somem.
        Entity& other = scene.CreateEntity("Outro");
        other.Add<Cronometro>();
        const EntityId otherId = other.id;
        runtime.Update(0.1f);
        CHECK(runtime.TimerCount() == 2);
        runtime.Destroy(otherId);
        runtime.Update(0.1f);
        runtime.Update(0.5f);
        CHECK(runtime.TimerCount() == 0);
        runtime.Stop();
    }

    SECTION("Mensagens: Send para a entidade da referência, Broadcast para todas; GetComponent");
    {
        g_log.clear();
        Scene scene;
        Entity& door = scene.CreateEntity("Porta");
        door.Add<Porta>();
        const EntityId doorId = door.id;
        Entity& button = scene.CreateEntity("Botao");
        button.Add<Botao>().porta = doorId;
        SceneRuntime runtime(scene);
        runtime.Start();
        CHECK(Comp<Porta>(scene, doorId).aberta && Comp<Porta>(scene, doorId).ultimoValor == 2.5f);
        CHECK(Logged("porta:abrir") && Logged("porta:alarme") && Logged("botao:alarme"));
        CHECK(Logged("botao:viu-aberta") && Logged("botao:achou-a-si") && Logged("botao:sem-porta-aqui"));
        CHECK(Logged("botao:ref-vazia"));
        runtime.Stop();
    }

    SECTION("Números aleatórios: intervalos, todas as faces do dado, semente repete a sequência");
    {
        Scene scene;
        Entity& a = scene.CreateEntity("A");
        a.Add<Dado>();
        Entity& b = scene.CreateEntity("B");
        b.Add<Dado>();
        const EntityId aId = a.id, bId = b.id;
        SceneRuntime runtime(scene);
        runtime.Start();
        CHECK(!Comp<Dado>(scene, aId).foraDoIntervalo);
        CHECK(Comp<Dado>(scene, aId).faces.size() == 6);
        CHECK(Comp<Dado>(scene, aId).sequencia == Comp<Dado>(scene, bId).sequencia);  // mesma semente
        runtime.Stop();
        CHECK(runtime.RandomInt(5, 5) == 5 && runtime.Random(1.0f, 1.0f) == 1.0f);
    }

    SECTION("Registro curto: MOTOR_FIELDS e rótulos tirados do nome");
    {
        const ComponentType* pulo = registry.Find("Pulo");
        CHECK(pulo && pulo->IsScript() && pulo->fields.size() == 3);
        if (pulo && pulo->fields.size() == 3) {
            CHECK(pulo->fields[0].key == "alturaMaxima" && pulo->fields[0].label == "Altura maxima");
            CHECK(pulo->fields[2].type == FieldType::Custom && pulo->fields[2].custom->name == "EntityRef");
        }
        CHECK(detail::LabelFromKey("velocidade") == "Velocidade");
        CHECK(detail::LabelFromKey("forca_do_pulo") == "Forca do pulo");
        CHECK(detail::LabelFromKey("corRGB") == "Cor RGB");
        CHECK(detail::LabelFromKey("vida2") == "Vida2");
        const ComponentType* empurrar = registry.Find("Empurrar");
        CHECK(empurrar && empurrar->fields[1].label == "Modo");
    }

    SECTION("EntityRef: salva e lê o id, e acompanha as cópias ao duplicar");
    {
        AssetLibrary assets;
        Scene scene;
        Entity& boss = scene.CreateEntity("Chefe");
        const EntityId bossId = boss.id;
        Entity& weapon = scene.CreateEntity("Arma", bossId);
        const EntityId weaponId = weapon.id;
        Entity& other = scene.CreateEntity("Outro");
        const EntityId otherId = other.id;
        scene.Find(bossId)->Add<Pulo>().alvo = weaponId;     // aponta para dentro da sub-árvore
        scene.Find(otherId)->Add<Pulo>().alvo = bossId;      // aponta para fora
        scene.Find(weaponId)->Add<Pulo>().alvo = otherId;

        const std::string json = SceneSerializer::ToJson(scene, assets);
        CHECK(json.find(std::format("\"alvo\": {}", weaponId)) != std::string::npos);
        Scene loaded;
        CHECK(SceneSerializer::FromJson(json, loaded, assets));
        CHECK(RefOf(loaded, bossId).id == weaponId);

        const EntityId copy = scene.Duplicate(bossId);
        const EntityId copyWeapon = scene.Children(copy).front();
        CHECK(RefOf(scene, copy).id == copyWeapon);       // a cópia do chefe usa a cópia da arma
        CHECK(RefOf(scene, copyWeapon).id == otherId);    // fora da cópia: igual
        CHECK(RefOf(scene, bossId).id == weaponId);       // a original não muda

        // Valor inválido pelo MCP/arquivo: recusado com o formato esperado.
        const FieldInfo& field = registry.Find("Pulo")->fields[2];
        std::string error;
        CHECK(!SceneSerializer::FieldValueFromJson(field, scene.Find(otherId)->Get<Pulo>(), "\"Chefe\"", assets, &error));
        CHECK(!error.empty());
        CHECK(SceneSerializer::FieldValueFromJson(field, scene.Find(otherId)->Get<Pulo>(), "null", assets));
        CHECK(RefOf(scene, otherId).IsEmpty());
    }

    SECTION("EntityRef em prefabs: criar, instanciar, sincronizar e overrides");
    {
        const fs::path root = TestDir(argc, argv, "test_script_api_tmp") / PathFromUtf8("Jogo Ação");
        fs::create_directories(root / "Assets" / "Prefabs");
        AssetLibrary assets;
        assets.SetRoot(root);
        PrefabLibrary library;
        Scene scene;
        Entity& enemy = scene.CreateEntity("Inimigo");
        const EntityId enemyId = enemy.id;
        Entity& eye = scene.CreateEntity("Olho", enemyId);
        const EntityId eyeId = eye.id;
        Entity& outside = scene.CreateEntity("Jogador");
        const EntityId outsideId = outside.id;
        scene.Find(enemyId)->Add<Pulo>().alvo = eyeId;
        scene.Find(eyeId)->Add<Pulo>().alvo = outsideId;  // fora do prefab

        std::string error;
        Prefab* prefab = Prefabs::CreateFromEntity(scene, enemyId, root / "Assets" / "Prefabs" / "Inimigo.mprefab",
                                                   library, assets, &error);
        CHECK(prefab != nullptr);
        if (prefab) {
            const EntityId prefabEye = prefab->scene.Children(prefab->root).front();
            CHECK(RefOf(prefab->scene, prefab->root).id == prefabEye);  // dentro do prefab: id do prefab
            CHECK(RefOf(prefab->scene, prefabEye).IsEmpty());          // fora: não vai para o arquivo
            // O original continua igual ao prefab, menos a referência para fora (o prefab não a guarda): vira
            // override do Olho e não se perde ao sincronizar.
            CHECK(Prefabs::RecordOverrides(scene, library, assets) == 1);
            const EntityId instance = Prefabs::Instantiate(*prefab, scene);
            const EntityId instanceEye = scene.Children(instance).front();
            CHECK(RefOf(scene, instance).id == instanceEye);  // cada instância aponta para o próprio olho
            CHECK(RefOf(scene, instanceEye).IsEmpty());
            CHECK(Prefabs::RecordOverrides(scene, library, assets) == 0);

            // Sincronizar (o prefab mudou outro campo): a referência continua da instância.
            prefab->scene.Find(prefab->root)->Get<Pulo>()->vidas = 9;
            library.MarkChanged(*prefab);
            CHECK(library.Save(*prefab, assets, &error));
            Prefabs::SyncInstances(scene, library, assets);
            CHECK(scene.Find(instance)->Get<Pulo>()->vidas == 9);
            CHECK(RefOf(scene, instance).id == instanceEye);
            CHECK(RefOf(scene, enemyId).id == eyeId);
            CHECK(RefOf(scene, eyeId).id == outsideId);  // override mantido
            CHECK(Prefabs::RecordOverrides(scene, library, assets) == 0);
        }
    }

    SECTION("Recarga: campo renomeado com formerKey mantém o valor; sumido ou de outro tipo vira aviso");
    {
        AssetLibrary assets;
        Scene scene;
        Entity& e = scene.CreateEntity("Caixa");
        ComponentPtr missing(ComponentRegistry::MissingType());
        auto& m = *static_cast<MissingComponent*>(missing.Data());
        m.type = "Empurrar";
        m.json = R"({"type": "Empurrar", "velocidade": 7.5, "modo": "rápido", "cor": [1, 0, 0]})";
        e.components.push_back(std::move(missing));
        const EntityId id = e.id;
        std::vector<SceneSerializer::FieldWarning> warnings;
        CHECK(SceneSerializer::ResolveMissingComponents(scene, assets, &warnings) == 1);
        CHECK(Comp<Empurrar>(scene, id).forca == 7.5f);  // pelo nome antigo
        CHECK(Comp<Empurrar>(scene, id).modo == 0);      // texto num int: fica o padrão
        CHECK(warnings.size() == 2);
        bool lost = false, changed = false;
        for (const auto& w : warnings) {
            CHECK(w.type == "Empurrar" && w.entity == "Caixa");
            if (w.field == "cor" && !w.typeChanged && w.value == "[1,0,0]") lost = true;
            if (w.field == "modo" && w.typeChanged && w.value == "\"rápido\"") changed = true;
        }
        CHECK(lost && changed);

        // Ao ler uma cena também vale o nome antigo.
        Scene loaded;
        CHECK(SceneSerializer::FromJson(
            R"({"format": "motor-scene", "version": 6, "entities": [{"id": 1, "name": "A", "components": [{"type": "Empurrar", "velocidade": 3.0}]}]})",
            loaded, assets));
        CHECK(loaded.Find(1) && loaded.Find(1)->Get<Empurrar>() && loaded.Find(1)->Get<Empurrar>()->forca == 3.0f);
    }

    return TestResult();
}
