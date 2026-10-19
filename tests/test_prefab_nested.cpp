// Prefab dentro de prefab (Fase 20): criar um prefab que contém a instância de outro, o de fora seguindo o de
// dentro (menos o que ele mudou), entidade nova no de dentro, gravar e reabrir, aplicar com um aninhado novo,
// recusar o prefab dentro dele mesmo e desfazer o vínculo voltando a ser instância do de dentro.
#include "TestUtil.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/core/Paths.h>
#include <motor/scene/Components.h>
#include <motor/scene/Prefab.h>
#include <motor/scene/Scene.h>

namespace fs = std::filesystem;
using namespace motor;

namespace {
const PrefabLink* LinkOf(const Entity* e) {
    const PrefabInstance* p = e ? e->Get<PrefabInstance>() : nullptr;
    return p ? &p->link : nullptr;
}

const PrefabLink* NestedOf(const Entity* e) {
    const PrefabNested* p = e ? e->Get<PrefabNested>() : nullptr;
    return p ? &p->link : nullptr;
}

Entity* Child(Scene& scene, EntityId parent, std::string_view name) {
    for (EntityId id : scene.Children(parent)) {
        if (scene.Find(id)->name == name) return scene.Find(id);
    }
    return nullptr;
}

glm::vec4 ColorOf(const Entity* e) {
    const MeshRenderer* mr = e ? e->Get<MeshRenderer>() : nullptr;
    return mr ? mr->material.color : glm::vec4(-1.0f);
}

// Um passo do editor: segue os prefabs (aninhados primeiro) e registra o que a cena mudou.
void EditorStep(Scene& scene, PrefabLibrary& library, AssetLibrary& assets) {
    if (Prefabs::NeedsSync(scene, library, assets)) Prefabs::SyncInstances(scene, library, assets);
    Prefabs::RecordOverrides(scene, library, assets);
}
} // namespace

int main(int argc, char** argv) {
    const fs::path root = TestDir(argc, argv, "test_prefab_nested_tmp") / PathFromUtf8("Vila Ação");
    fs::create_directories(root / "Assets" / "Prefabs");
    std::string error;

    AssetLibrary assets;
    assets.SetRoot(root);
    PrefabLibrary library;
    Scene scene;

    SECTION("Prefab de dentro: Janela (azul)");
    {
        Entity& janela = scene.CreateEntity("Janela");
        janela.Add<MeshRenderer>().material.color = {0, 0, 1, 1};
    }
    const fs::path janelaFile = root / "Assets" / "Prefabs" / "Janela.mprefab";
    Prefab* janela = Prefabs::CreateFromEntity(scene, scene.FindByName("Janela")->id, janelaFile, library, assets, &error);
    CHECK(janela != nullptr);
    if (!janela) return TestResult();
    const std::string janelaGuid = janela->guid;
    scene.DestroyEntity(scene.FindByName("Janela")->id);

    SECTION("Casa com uma Janela (cor mudada na instância) vira prefab: a Janela fica aninhada");
    const EntityId casaId = scene.CreateEntity("Casa").id;
    scene.CreateEntity("Porta", casaId);
    const EntityId janelaInst = Prefabs::Instantiate(*janela, scene, casaId);
    scene.Find(janelaInst)->transform.position = {1, 2, 0};
    scene.Find(janelaInst)->Get<MeshRenderer>()->material.color = {1, 0, 0, 1};  // vermelha nesta casa
    EditorStep(scene, library, assets);
    CHECK(LinkOf(scene.Find(janelaInst))->HasOverride("MeshRenderer.color"));

    const fs::path casaFile = root / "Assets" / "Prefabs" / PathFromUtf8("Casa Ação.mprefab");
    Prefab* casa = Prefabs::CreateFromEntity(scene, casaId, casaFile, library, assets, &error);
    CHECK(casa != nullptr);
    if (!casa) return TestResult();
    const std::string casaGuid = casa->guid;
    CHECK(casa->scene.Entities().size() == 3);
    {
        Entity* inner = Child(casa->scene, casa->root, "Janela");
        CHECK(inner && LinkOf(inner)->guid == casaGuid);  // identidade dentro da Casa
        CHECK(NestedOf(inner) && NestedOf(inner)->guid == janelaGuid);
        CHECK(NestedOf(inner) && NestedOf(inner)->HasOverride("MeshRenderer.color"));
        CHECK(ColorOf(inner) == glm::vec4(1, 0, 0, 1));
        CHECK(!NestedOf(Child(casa->scene, casa->root, "Porta")));
    }
    // Na cena, a Janela agora segue a Casa (e guarda de onde veio).
    CHECK(LinkOf(scene.Find(janelaInst))->guid == casaGuid);
    CHECK(NestedOf(scene.Find(janelaInst)) && NestedOf(scene.Find(janelaInst))->guid == janelaGuid);

    // Segunda casa.
    const EntityId casa2 = Prefabs::Instantiate(*casa, scene);
    EditorStep(scene, library, assets);
    CHECK(ColorOf(Child(scene, casa2, "Janela")) == glm::vec4(1, 0, 0, 1));

    SECTION("A Janela muda: a Casa e as casas da cena seguem, menos a cor que a Casa mudou");
    {
        Entity& jr = *janela->scene.Find(janela->root);
        jr.transform.scale = {2, 2, 2};
        jr.Get<MeshRenderer>()->material.color = {0, 1, 0, 1};  // verde: a Casa mudou a cor, não segue
        library.MarkChanged(*janela);
    }
    const uint64_t casaRevision = casa->revision;
    EditorStep(scene, library, assets);
    CHECK(casa->revision != casaRevision);  // o conteúdo da Casa mudou
    CHECK(Child(casa->scene, casa->root, "Janela")->transform.scale == glm::vec3(2, 2, 2));
    CHECK(ColorOf(Child(casa->scene, casa->root, "Janela")) == glm::vec4(1, 0, 0, 1));
    for (EntityId c : {casaId, casa2}) {
        Entity* j = Child(scene, c, "Janela");
        CHECK(j && j->transform.scale == glm::vec3(2, 2, 2));
        CHECK(ColorOf(j) == glm::vec4(1, 0, 0, 1));
    }
    CHECK(Child(scene, casa2, "Janela")->transform.position == glm::vec3(1, 2, 0));  // posição da Casa

    SECTION("A Janela ganha um Vidro: aparece na Casa e nas casas");
    {
        Entity& vidro = janela->scene.CreateEntity("Vidro", janela->root);
        vidro.Add<PrefabInstance>().link = {janelaGuid, 77, {}, 0};
        library.MarkChanged(*janela);
    }
    EditorStep(scene, library, assets);
    {
        Entity* inner = Child(casa->scene, casa->root, "Janela");
        Entity* vidro = inner ? Child(casa->scene, inner->id, "Vidro") : nullptr;
        CHECK(vidro && LinkOf(vidro)->guid == casaGuid && LinkOf(vidro)->source > 3);
        CHECK(vidro && NestedOf(vidro) && NestedOf(vidro)->guid == janelaGuid && NestedOf(vidro)->source == 77);
        Entity* j2 = Child(scene, casa2, "Janela");
        CHECK(j2 && Child(scene, j2->id, "Vidro"));
    }

    SECTION("Editar a Janela dentro da Casa (Inspector do prefab) vira override do aninhado");
    {
        Entity* inner = Child(casa->scene, casa->root, "Janela");
        inner->name = "Janela Grande";
        CHECK(Prefabs::RecordNestedOverrides(*casa, library, assets) == 1);
        CHECK(NestedOf(inner)->HasOverride("name"));
        library.MarkChanged(*casa);
        janela->scene.Find(janela->root)->transform.scale = {3, 3, 3};
        library.MarkChanged(*janela);
        EditorStep(scene, library, assets);
        CHECK(inner->name == "Janela Grande" && inner->transform.scale == glm::vec3(3, 3, 3));
        CHECK(Child(scene, casa2, "Janela Grande"));
    }

    SECTION("Gravar e reabrir: o vínculo aninhado vem do arquivo");
    CHECK(library.Save(*casa, assets, &error));
    {
        PrefabLibrary fresh;
        Prefab* reopened = fresh.Get(casaGuid, assets, &error);
        CHECK(reopened != nullptr);
        Entity* inner = reopened ? Child(reopened->scene, reopened->root, "Janela Grande") : nullptr;
        CHECK(inner && NestedOf(inner) && NestedOf(inner)->guid == janelaGuid);
        CHECK(inner && NestedOf(inner)->HasOverride("MeshRenderer.color") && NestedOf(inner)->HasOverride("name"));
        // A Janela mudou depois de a Casa ser gravada? Ao abrir, a Casa segue a Janela do disco.
        Prefabs::UpdateNested(fresh, assets);
        CHECK(inner && ColorOf(inner) == glm::vec4(1, 0, 0, 1));
    }

    SECTION("Aplicar a casa com mais uma Janela posta nela: fica aninhada também");
    {
        const EntityId extra = Prefabs::Instantiate(*janela, scene, casa2);
        scene.Find(extra)->name = "Janela Extra";
        EditorStep(scene, library, assets);
        CHECK(Prefabs::ApplyInstance(scene, casa2, library, assets, &error));
        Entity* inner = Child(casa->scene, casa->root, "Janela Extra");
        CHECK(inner && NestedOf(inner) && NestedOf(inner)->guid == janelaGuid);
        CHECK(inner && NestedOf(inner)->HasOverride("name"));
        CHECK(LinkOf(scene.Find(extra))->guid == casaGuid);
        EditorStep(scene, library, assets);
        CHECK(Child(scene, casaId, "Janela Extra"));  // a outra casa ganhou a janela nova
    }

    SECTION("Prefab dentro dele mesmo é recusado");
    {
        // Uma Casa dentro de uma Janela, aplicada na Janela: a Casa já contém a Janela.
        const EntityId solta = Prefabs::Instantiate(*janela, scene);
        Prefabs::Instantiate(*casa, scene, solta);
        EditorStep(scene, library, assets);
        const size_t before = janela->scene.Entities().size();
        error.clear();
        CHECK(!Prefabs::ApplyInstance(scene, solta, library, assets, &error));
        CHECK(!error.empty());
        CHECK(janela->scene.Entities().size() == before);
        scene.DestroyEntity(solta);
    }

    SECTION("Desvincular a casa: as janelas voltam a ser instâncias da Janela");
    {
        Entity* j = Child(scene, casa2, "Janela Grande");
        const EntityId jId = j ? j->id : kNullEntity;
        Prefabs::Unpack(scene, casa2, library, assets);
        CHECK(!LinkOf(scene.Find(casa2)));
        CHECK(LinkOf(scene.Find(jId)) && LinkOf(scene.Find(jId))->guid == janelaGuid);
        CHECK(!NestedOf(scene.Find(jId)));
        CHECK(LinkOf(scene.Find(jId)) && LinkOf(scene.Find(jId))->HasOverride("MeshRenderer.color"));
        CHECK(Prefabs::IsInstanceRoot(scene, jId, library, assets));
        // E seguem a Janela daqui em diante.
        janela->scene.Find(janela->root)->transform.scale = {4, 4, 4};
        library.MarkChanged(*janela);
        EditorStep(scene, library, assets);
        CHECK(scene.Find(jId)->transform.scale == glm::vec3(4, 4, 4));
        CHECK(ColorOf(scene.Find(jId)) == glm::vec4(1, 0, 0, 1));
    }

    return TestResult();
}
