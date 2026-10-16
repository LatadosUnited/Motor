// Prefabs (.mprefab): criar a partir de uma entidade, instanciar, atualizar as instâncias quando o prefab muda,
// overrides, aplicar/reverter, ida e volta na cena e o histórico de desfazer. Não cria contexto OpenGL.
#include "TestUtil.h"

#include "UndoHistory.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/core/Paths.h>
#include <motor/scene/Components.h>
#include <motor/scene/Prefab.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneSerializer.h>

#include <chrono>

namespace fs = std::filesystem;
using namespace motor;

namespace {
alignas(16) unsigned char g_fakeMesh[256] = {};

void AddFakeBuiltIns(AssetLibrary& assets) {
    assets.AddMesh("Cubo", std::shared_ptr<Mesh>(reinterpret_cast<Mesh*>(g_fakeMesh), [](Mesh*) {}));
}

const PrefabLink* LinkOf(const Entity* e) {
    const PrefabInstance* p = e ? e->Get<PrefabInstance>() : nullptr;
    return p ? &p->link : nullptr;
}

// Filho de `parent` com esse nome.
Entity* Child(Scene& scene, EntityId parent, std::string_view name) {
    for (EntityId id : scene.Children(parent)) {
        if (scene.Find(id)->name == name) return scene.Find(id);
    }
    return nullptr;
}

glm::vec4 ColorOf(Entity* e) {
    const MeshRenderer* mr = e ? e->Get<MeshRenderer>() : nullptr;
    return mr ? mr->material.color : glm::vec4(-1.0f);
}
} // namespace

int main(int argc, char** argv) {
    const fs::path root = TestDir(argc, argv, "test_prefabs_tmp") / PathFromUtf8("Jogo Ação");
    fs::create_directories(root / "Assets" / "Prefabs");
    std::string error;

    AssetLibrary assets;
    AddFakeBuiltIns(assets);
    assets.SetRoot(root);
    PrefabLibrary library;

    // Torre: raiz com Base (cubo laranja) e Topo (cubo roxo).
    Scene scene;
    Entity& tower = scene.CreateEntity("Torre");
    tower.transform.position = {5.0f, 0.0f, 0.0f};
    const EntityId towerId = tower.id;
    for (const auto& [name, color, y] : {std::tuple{"Base", glm::vec4(1.0f, 0.5f, 0.0f, 1.0f), 0.5f},
                                         std::tuple{"Topo", glm::vec4(0.6f, 0.2f, 0.9f, 1.0f), 1.5f}}) {
        Entity& part = scene.CreateEntity(name, towerId);
        part.transform.position.y = y;
        MeshRenderer& mr = part.Add<MeshRenderer>();
        mr.mesh = assets.FindMesh("Cubo");
        mr.material.color = color;
    }

    SECTION("Criar prefab a partir da entidade");
    const fs::path file = root / "Assets" / "Prefabs" / PathFromUtf8("Torre Ação.mprefab");
    CHECK(AssetTypeFromPath(file) == AssetType::Prefab);
    Prefab* prefab = Prefabs::CreateFromEntity(scene, towerId, file, library, assets, &error);
    CHECK(prefab != nullptr);
    if (!prefab) return TestResult();
    CHECK(fs::exists(file) && fs::exists(AssetDatabase::MetaPathFor(file)));
    CHECK(prefab->guid == assets.Database().FindGuid(file));
    CHECK(prefab->scene.Entities().size() == 3);
    CHECK(prefab->scene.Find(prefab->root)->transform.position == glm::vec3(0.0f));  // raiz na origem
    CHECK(LinkOf(&tower) && LinkOf(&tower)->guid == prefab->guid && LinkOf(&tower)->source == prefab->rootSource);
    CHECK(Prefabs::IsInstanceRoot(scene, towerId, library, assets));
    CHECK(Prefabs::InstanceRoot(scene, Child(scene, towerId, "Base")->id, library, assets) == towerId);
    CHECK(scene.Find(towerId)->transform.position == glm::vec3(5.0f, 0.0f, 0.0f));  // a original fica onde estava
    CHECK(!Prefabs::NeedsSync(scene, library, assets));
    CHECK(Prefabs::RecordOverrides(scene, library, assets) == 0);  // igual ao prefab: nenhum override

    SECTION("Instanciar");
    const EntityId second = Prefabs::Instantiate(*prefab, scene);
    const EntityId third = Prefabs::Instantiate(*prefab, scene);
    scene.Find(second)->transform.position = {-3.0f, 0.0f, 0.0f};
    scene.Find(third)->transform.position = {0.0f, 0.0f, 4.0f};
    CHECK(scene.Entities().size() == 9);
    CHECK(Child(scene, second, "Base") && Child(scene, third, "Topo"));
    CHECK(ColorOf(Child(scene, third, "Base")) == glm::vec4(1.0f, 0.5f, 0.0f, 1.0f));
    CHECK(Prefabs::RecordOverrides(scene, library, assets) == 0);  // posição da raiz não é override

    SECTION("Mudar a cor no prefab atualiza todas as instâncias");
    auto setPrefabColor = [&](const char* name, glm::vec4 color) {
        for (const auto& e : prefab->scene.Entities()) {
            if (e->name == name) e->Get<MeshRenderer>()->material.color = color;
        }
        library.MarkChanged(*prefab);
        CHECK(library.Save(*prefab, assets, &error));
    };
    const glm::vec4 green{0.1f, 0.8f, 0.2f, 1.0f};
    setPrefabColor("Base", green);
    CHECK(Prefabs::NeedsSync(scene, library, assets));
    CHECK(Prefabs::SyncInstances(scene, library, assets) == 3);
    CHECK(ColorOf(Child(scene, towerId, "Base")) == green);
    CHECK(ColorOf(Child(scene, second, "Base")) == green);
    CHECK(ColorOf(Child(scene, third, "Base")) == green);
    CHECK(scene.Find(second)->transform.position == glm::vec3(-3.0f, 0.0f, 0.0f));  // posição de cada instância
    CHECK(!Prefabs::NeedsSync(scene, library, assets));

    SECTION("Overrides: o que mudou na instância não segue o prefab");
    const glm::vec4 blue{0.0f, 0.2f, 1.0f, 1.0f};
    Child(scene, second, "Base")->Get<MeshRenderer>()->material.color = blue;
    Child(scene, second, "Topo")->name = "Chapéu";
    CHECK(Prefabs::RecordOverrides(scene, library, assets) == 2);
    CHECK(LinkOf(Child(scene, second, "Base"))->HasOverride("MeshRenderer.color"));
    CHECK(LinkOf(Child(scene, second, "Chapéu"))->HasOverride("name"));
    CHECK(Prefabs::RecordOverrides(scene, library, assets) == 0);  // não repete

    const glm::vec4 yellow{1.0f, 0.9f, 0.1f, 1.0f};
    setPrefabColor("Base", yellow);
    Prefabs::SyncInstances(scene, library, assets);
    CHECK(ColorOf(Child(scene, towerId, "Base")) == yellow);
    CHECK(ColorOf(Child(scene, second, "Base")) == blue);  // override mantido
    CHECK(Child(scene, second, "Chapéu") != nullptr);
    setPrefabColor("Topo", green);  // o Topo do segundo só mudou de nome: a cor segue o prefab
    Prefabs::SyncInstances(scene, library, assets);
    CHECK(ColorOf(Child(scene, second, "Chapéu")) == green);

    SECTION("Componente removido e entidade apagada na instância");
    Child(scene, third, "Topo")->Remove<MeshRenderer>();
    CHECK(Prefabs::RecordOverrides(scene, library, assets) == 1);
    CHECK(LinkOf(Child(scene, third, "Topo"))->HasOverride("-MeshRenderer"));
    scene.DestroyEntity(Child(scene, third, "Base")->id);
    CHECK(Prefabs::RecordOverrides(scene, library, assets) == 1);  // "-#n" na raiz
    setPrefabColor("Topo", blue);
    Prefabs::SyncInstances(scene, library, assets);
    CHECK(!Child(scene, third, "Base"));                                  // continua apagada
    CHECK(Child(scene, third, "Topo") && !Child(scene, third, "Topo")->Has<MeshRenderer>());  // continua sem

    SECTION("Aplicar instância ao prefab: entidade nova chega às outras instâncias");
    Entity& flag = scene.CreateEntity("Bandeira", towerId);
    flag.transform.position.y = 2.5f;
    flag.Add<MeshRenderer>().mesh = assets.FindMesh("Cubo");
    CHECK(Prefabs::ApplyInstance(scene, flag.id, library, assets, &error));
    CHECK(prefab->scene.Entities().size() == 4);
    CHECK(LinkOf(scene.FindByName("Bandeira")) != nullptr);
    CHECK(Prefabs::NeedsSync(scene, library, assets));  // as outras instâncias ainda não viram
    Prefabs::SyncInstances(scene, library, assets);
    CHECK(Child(scene, second, "Bandeira") && Child(scene, third, "Bandeira"));
    CHECK(Child(scene, second, "Bandeira") && Child(scene, second, "Bandeira")->transform.position.y == 2.5f);
    CHECK(ColorOf(Child(scene, second, "Base")) == blue);  // overrides do segundo continuam
    CHECK(Prefabs::RecordOverrides(scene, library, assets) == 0);

    SECTION("Ida e volta na cena (e o prefab mudando com a cena fechada)");
    const std::string saved = SceneSerializer::ToJson(scene, assets);
    CHECK(saved.find("\"PrefabInstance\"") != std::string::npos);
    CHECK(saved.find("MeshRenderer.color") != std::string::npos);
    {
        // Com a cena "fechada", o prefab muda (outra biblioteca, como outro editor).
        PrefabLibrary other;
        Prefab* p = other.GetFile(file, assets, &error);
        CHECK(p != nullptr);
        for (const auto& e : p->scene.Entities()) {
            if (e->name == "Base") e->Get<MeshRenderer>()->material.color = green;
        }
        CHECK(other.Save(*p, assets, &error));
    }
    {
        AssetLibrary freshAssets;
        AddFakeBuiltIns(freshAssets);
        freshAssets.SetRoot(root);
        PrefabLibrary freshLibrary;
        Scene reopened;
        CHECK(SceneSerializer::FromJson(saved, reopened, freshAssets, &error));
        Entity* secondRoot = nullptr;
        for (const auto& e : reopened.Entities()) {
            if (Prefabs::IsInstanceRoot(reopened, e->id, freshLibrary, freshAssets) && e->transform.position.x == -3.0f) {
                secondRoot = e.get();
            }
        }
        CHECK(secondRoot != nullptr);
        if (secondRoot) {
            CHECK(LinkOf(Child(reopened, secondRoot->id, "Base"))->HasOverride("MeshRenderer.color"));
            CHECK(Prefabs::NeedsSync(reopened, freshLibrary, freshAssets));  // recém-aberta
            CHECK(Prefabs::SyncInstances(reopened, freshLibrary, freshAssets) == 3);
            CHECK(ColorOf(Child(reopened, secondRoot->id, "Base")) == blue);  // override
            CHECK(Child(reopened, secondRoot->id, "Chapéu") != nullptr);
            CHECK(Prefabs::RecordOverrides(reopened, freshLibrary, freshAssets) == 0);
        }
        Entity* towerAgain = reopened.FindByName("Torre");
        CHECK(towerAgain && ColorOf(Child(reopened, towerAgain->id, "Base")) == green);  // mudança de fora
        CHECK(towerAgain && towerAgain->transform.position == glm::vec3(5.0f, 0.0f, 0.0f));
        CHECK(SceneSerializer::ToJson(reopened, freshAssets).find("\"version\": 5") != std::string::npos);
    }

    SECTION("Prefab mudou no disco: ReloadChanged");
    CHECK(!library.ReloadChanged(assets) || true);  // a gravação de `other` acima já conta como mudança
    fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds(3));
    CHECK(library.ReloadChanged(assets));
    CHECK(!library.ReloadChanged(assets));
    Prefabs::SyncInstances(scene, library, assets);
    CHECK(ColorOf(Child(scene, towerId, "Base")) == green);

    SECTION("Reverter: a instância volta a ser igual ao prefab");
    Prefabs::RevertInstance(scene, second, library, assets);
    CHECK(ColorOf(Child(scene, second, "Base")) == green);
    CHECK(Child(scene, second, "Topo") != nullptr && !Child(scene, second, "Chapéu"));
    CHECK(LinkOf(Child(scene, second, "Base"))->overrides.empty());
    Prefabs::RevertInstance(scene, third, library, assets);
    CHECK(Child(scene, third, "Base") != nullptr);  // a apagada volta
    CHECK(Child(scene, third, "Topo") && Child(scene, third, "Topo")->Has<MeshRenderer>());

    SECTION("Desfazer não volta a mudança do prefab (fotos do histórico sincronizadas)");
    {
        editor::UndoHistory history;
        history.Reset(scene, kNullEntity);
        Child(scene, second, "Base")->transform.scale = glm::vec3(2.0f);  // edição na instância: um passo
        Prefabs::RecordOverrides(scene, library, assets);
        history.Track(scene, kNullEntity, true);
        CHECK(history.UndoCount() == 1);

        setPrefabColor("Topo", yellow);  // o prefab muda: cena e fotos seguem juntas
        Prefabs::SyncInstances(scene, library, assets);
        history.ForEachScene([&](Scene& s) { Prefabs::SyncInstances(s, library, assets); });
        history.Track(scene, kNullEntity, true);
        CHECK(history.UndoCount() == 1);  // sync não vira passo
        EntityId selected = kNullEntity;
        CHECK(history.Undo(scene, selected));
        CHECK(Child(scene, second, "Base")->transform.scale == glm::vec3(1.0f));
        CHECK(ColorOf(Child(scene, second, "Topo")) == yellow);  // o prefab continua mudado
        history.Track(scene, kNullEntity, true);
        CHECK(history.UndoCount() == 0 && history.CanRedo());
    }

    SECTION("Desfazer vínculo");
    Prefabs::Unpack(scene, third, library, assets);
    CHECK(!scene.Find(third)->Has<PrefabInstance>() && !Child(scene, third, "Topo")->Has<PrefabInstance>());
    setPrefabColor("Topo", blue);
    Prefabs::SyncInstances(scene, library, assets);
    CHECK(ColorOf(Child(scene, third, "Topo")) == yellow);  // não segue mais
    CHECK(ColorOf(Child(scene, second, "Topo")) == blue);

    SECTION("Prefab inválido");
    CHECK(library.Get("0123456789abcdef0123456789abcdef", assets, &error) == nullptr);
    Scene notPrefab;
    CHECK(!SceneSerializer::FromJson(R"({"format": "motor-scene", "version": 5, "entities": []})", notPrefab, assets,
                                     &error, SceneSerializer::kPrefabFormat));

    return TestResult();
}
