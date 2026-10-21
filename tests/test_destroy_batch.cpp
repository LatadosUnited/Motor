// Destruir em lote (Fase 25): Scene::DestroyEntities e Scene::Subtrees tiram várias sub-árvores numa passada só, e o
// SceneRuntime destrói assim o que os scripts pediram no passo. Confere o mesmo resultado de destruir uma por uma:
// descendentes juntos (também filho que vem antes do pai na cena), pedidos repetidos ou aninhados, OnDestroy na
// ordem dos pedidos e destruição pedida dentro do OnDestroy.
#include "TestUtil.h"

#include <motor/scene/Component.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>

#include <string>
#include <vector>

using namespace motor;

namespace {
std::vector<std::string> g_events;

struct Fim : Script {
    std::string tag = "?";
    std::string levar;  // nome de outra entidade para destruir no OnDestroy
    void OnDestroy() override {
        g_events.push_back(tag);
        if (!levar.empty()) {
            if (Entity* e = FindEntity(levar)) Destroy(e->id);
        }
    }
};

// No primeiro OnUpdate, pede para destruir as entidades da lista (na ordem).
struct Destruidor : Script {
    std::vector<std::string> nomes;
    void OnUpdate(float) override {
        for (const std::string& n : nomes) {
            if (Entity* e = FindEntity(n)) Destroy(e->id);
        }
        nomes.clear();
    }
};

void Register() {
    auto& registry = ComponentRegistry::Instance();
    registry.Register<Fim>("Fim", "Fim").Field("tag", "Tag", &Fim::tag).Field("levar", "Levar", &Fim::levar);
    registry.Register<Destruidor>("Destruidor", "Destruidor");
}

std::vector<std::string> Names(const Scene& scene) {
    std::vector<std::string> names;
    for (const auto& e : scene.Entities()) names.push_back(e->name);
    return names;
}

EntityId Make(Scene& scene, const std::string& name, EntityId parent = kNullEntity) {
    return scene.CreateEntity(name, parent).id;
}
} // namespace

int main() {
    Register();

    SECTION("DestroyEntities: várias sub-árvores, repetidas, aninhadas e inexistentes");
    {
        Scene scene;
        const EntityId a = Make(scene, "A");
        const EntityId a1 = Make(scene, "A1", a);
        Make(scene, "A1x", a1);
        const EntityId b = Make(scene, "B");
        Make(scene, "B1", b);
        const EntityId c = Make(scene, "C");
        const uint64_t v = SceneStructureVersion();
        scene.DestroyEntities({a1, a, b, b, 9999});
        CHECK((Names(scene) == std::vector<std::string>{"C"}));
        CHECK(SceneStructureVersion() > v);
        CHECK(scene.Find(a) == nullptr && scene.Find(a1) == nullptr && scene.Find(c) != nullptr);

        const uint64_t v2 = SceneStructureVersion();
        scene.DestroyEntities({9999});  // nada a destruir: a estrutura não muda
        CHECK(SceneStructureVersion() == v2);
    }

    SECTION("Subtrees: raiz e descendentes na ordem da cena, raízes na ordem pedida");
    {
        Scene scene;
        const EntityId filho = Make(scene, "Filho");  // criado antes do pai
        const EntityId pai = Make(scene, "Pai");
        scene.SetParent(filho, pai);
        const EntityId x = Make(scene, "X");
        const EntityId neto = Make(scene, "Neto", filho);
        const EntityId y = Make(scene, "Y");
        CHECK((scene.Subtrees({y, pai}) == std::vector<EntityId>{y, pai, filho, neto}));
        CHECK((scene.Subtrees({filho, pai}) == std::vector<EntityId>{pai, filho, neto}));  // filho vai com o pai
        scene.DestroyEntities({pai});
        CHECK((Names(scene) == std::vector<std::string>{"X", "Y"}));
        CHECK(scene.Find(x) != nullptr);
    }

    SECTION("DestroyEntity continua igual (uma sub-árvore)");
    {
        Scene scene;
        const EntityId a = Make(scene, "A");
        Make(scene, "A1", a);
        Make(scene, "B");
        scene.DestroyEntity(a);
        CHECK((Names(scene) == std::vector<std::string>{"B"}));
    }

    SECTION("SceneRuntime: OnDestroy na ordem dos pedidos, descendentes junto, e pedido dentro do OnDestroy");
    {
        g_events.clear();
        Scene scene;
        const EntityId p = Make(scene, "P");
        scene.Find(p)->Add<Fim>().tag = "p";
        const EntityId p1 = Make(scene, "P1", p);
        scene.Find(p1)->Add<Fim>().tag = "p1";
        const EntityId q = Make(scene, "Q");
        Fim& fq = scene.Find(q)->Add<Fim>();
        fq.tag = "q";
        fq.levar = "R";  // R só é pedido no OnDestroy de Q: vai na rodada seguinte
        scene.Find(Make(scene, "R"))->Add<Fim>().tag = "r";
        scene.Find(Make(scene, "Fica"))->Add<Fim>().tag = "fica";
        Destruidor& d = scene.Find(Make(scene, "Diretor"))->Add<Destruidor>();
        d.nomes = {"Q", "P1", "P", "Q"};

        SceneRuntime runtime(scene);
        runtime.Start();
        runtime.Update(0.1f);
        CHECK((g_events == std::vector<std::string>{"q", "p", "p1", "r"}));
        CHECK((Names(scene) == std::vector<std::string>{"Fica", "Diretor"}));
        runtime.Stop();
        CHECK(g_events.back() == "fica");
    }

    SECTION("Muitas entidades destruídas no mesmo passo");
    {
        g_events.clear();
        Scene scene;
        std::vector<std::string> nomes;
        for (int i = 0; i < 300; ++i) {
            const std::string n = "Cubo " + std::to_string(i);
            const EntityId id = Make(scene, n);
            Make(scene, n + " filho", id);
            if (i % 2 == 0) nomes.push_back(n);
        }
        scene.Find(Make(scene, "Diretor"))->Add<Destruidor>().nomes = nomes;
        SceneRuntime runtime(scene);
        runtime.Start();
        runtime.Update(0.1f);
        CHECK(scene.Entities().size() == 301);  // 150 cubos com filho + o diretor
        CHECK(scene.FindByName("Cubo 0") == nullptr && scene.FindByName("Cubo 0 filho") == nullptr);
        CHECK(scene.FindByName("Cubo 1") != nullptr && scene.FindByName("Cubo 1 filho") != nullptr);
        runtime.Stop();
    }

    return TestResult();
}
