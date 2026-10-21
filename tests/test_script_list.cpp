// Lista de scripts do SceneRuntime (Fase 22): os laços dos scripts usam a lista das entidades que têm script, refeita
// quando a estrutura da cena muda (SceneStructureVersion), sem percorrer a cena inteira. Confere que o resultado é o
// mesmo de quando o laço percorria tudo: ordem, scripts adicionados e removidos durante o jogo, entidades criadas e
// destruídas, cena trocada e mensagens.
#include "TestUtil.h"

#include <motor/scene/Component.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace motor;

namespace {
std::vector<std::string> g_events;

size_t Count(const std::string& event) {
    return static_cast<size_t>(std::count(g_events.begin(), g_events.end(), event));
}

struct Marca : Script {
    std::string tag = "?";
    void OnStart() override { g_events.push_back(tag + ":start"); }
    void OnUpdate(float) override { g_events.push_back(tag + ":update"); }
    void OnFixedUpdate(float) override { g_events.push_back(tag + ":fixed"); }
    void OnMessage(const Message& m) override { g_events.push_back(tag + ":msg:" + std::string(m.text)); }
};

// No OnStart, põe uma Marca na entidade "Vizinha" (que já existe e não tem script).
struct Contagiar : Script {
    void OnStart() override {
        g_events.push_back("contagiar:start");
        if (Entity* v = FindEntity("Vizinha")) v->Add<Marca>().tag = "vizinha";
    }
};

// No primeiro OnUpdate, cria uma entidade com Marca (só deve começar no próximo passo).
struct Criador : Script {
    bool criou = false;
    void OnUpdate(float) override {
        if (criou) return;
        criou = true;
        CreateEntity("Nova").Add<Marca>().tag = "nova";
    }
};

void Register() {
    auto& registry = ComponentRegistry::Instance();
    registry.Register<Marca>("Marca", "Marca").Field("tag", "Tag", &Marca::tag);
    registry.Register<Contagiar>("Contagiar", "Contagiar");
    registry.Register<Criador>("Criador", "Criador");
}

// Acrescenta `n` entidades sem script.
void Encher(Scene& scene, int n) {
    for (int i = 0; i < n; ++i) scene.CreateEntity("Cubo " + std::to_string(i));
}
} // namespace

int main() {
    Register();

    SECTION("O contador de estrutura sobe ao criar, destruir, adicionar componente e copiar a cena");
    {
        Scene scene;
        uint64_t v = SceneStructureVersion();
        Entity& e = scene.CreateEntity("A");
        const EntityId id = e.id;
        CHECK(SceneStructureVersion() > v);
        v = SceneStructureVersion();
        scene.Find(id)->Add<Marca>();
        CHECK(SceneStructureVersion() > v);
        v = SceneStructureVersion();
        scene.Find(id)->Add<Marca>();  // já tinha: não é mudança
        CHECK(SceneStructureVersion() == v);
        Scene copy = scene;
        CHECK(SceneStructureVersion() > v);
        v = SceneStructureVersion();
        scene.DestroyEntity(id);
        CHECK(SceneStructureVersion() > v);
    }

    SECTION("Só as entidades com script rodam, na ordem da cena, entre muitas sem script");
    {
        g_events.clear();
        Scene scene;
        Encher(scene, 500);
        scene.CreateEntity("S1").Add<Marca>().tag = "s1";
        Encher(scene, 500);
        scene.CreateEntity("S2").Add<Marca>().tag = "s2";
        SceneRuntime runtime(scene);
        runtime.Start();
        runtime.FixedUpdate(0.02f);
        runtime.Update(0.1f);
        CHECK((g_events == std::vector<std::string>{"s1:start", "s2:start", "s1:fixed", "s2:fixed", "s1:update",
                                                    "s2:update"}));
        runtime.Stop();
    }

    SECTION("Script adicionado durante o jogo a uma entidade sem script começa no próximo passo");
    {
        g_events.clear();
        Scene scene;
        scene.CreateEntity("Com").Add<Marca>().tag = "com";
        const EntityId sem = scene.CreateEntity("Sem").id;
        SceneRuntime runtime(scene);
        runtime.Start();
        runtime.Update(0.1f);
        scene.Find(sem)->Add<Marca>().tag = "tarde";
        g_events.clear();
        runtime.Update(0.1f);
        CHECK((g_events == std::vector<std::string>{"tarde:start", "com:update", "tarde:update"}));
        runtime.Stop();
    }

    SECTION("OnStart que põe script em outra entidade que já existia: começa na mesma passada");
    {
        g_events.clear();
        Scene scene;
        scene.CreateEntity("Contagiante").Add<Contagiar>();
        scene.CreateEntity("Vizinha");
        SceneRuntime runtime(scene);
        runtime.Start();
        CHECK((g_events == std::vector<std::string>{"contagiar:start", "vizinha:start"}));
        runtime.Update(0.1f);
        CHECK(Count("vizinha:update") == 1);
        runtime.Stop();
    }

    SECTION("Entidade criada por um script durante o laço só começa no próximo passo");
    {
        g_events.clear();
        Scene scene;
        scene.CreateEntity("Criador").Add<Criador>();
        SceneRuntime runtime(scene);
        runtime.Start();
        runtime.Update(0.1f);  // cria "Nova" no OnUpdate
        CHECK(Count("nova:start") == 0 && Count("nova:update") == 0);
        runtime.Update(0.1f);
        CHECK(Count("nova:start") == 1 && Count("nova:update") == 1);
        runtime.Stop();
    }

    SECTION("Script removido e entidade destruída saem da lista");
    {
        g_events.clear();
        Scene scene;
        const EntityId a = scene.CreateEntity("A").id;
        scene.Find(a)->Add<Marca>().tag = "a";
        const EntityId b = scene.CreateEntity("B").id;
        scene.Find(b)->Add<Marca>().tag = "b";
        SceneRuntime runtime(scene);
        runtime.Start();
        runtime.Update(0.1f);

        // Removido direto do vetor, como o Inspector faz (sem subir o contador): o laço confere na hora.
        scene.Find(a)->components.clear();
        runtime.Destroy(b);
        runtime.Update(0.1f);  // b ainda roda neste quadro; a destruição acontece no fim dele
        g_events.clear();
        runtime.Update(0.1f);
        CHECK(g_events.empty());
        runtime.Stop();
    }

    SECTION("Cena trocada inteira (como ao restaurar uma cópia) refaz a lista");
    {
        g_events.clear();
        Scene scene;
        scene.CreateEntity("A").Add<Marca>().tag = "a";
        Scene outra;
        Encher(outra, 3);
        outra.CreateEntity("X").Add<Marca>().tag = "x";
        SceneRuntime runtime(scene);
        runtime.Start();
        runtime.Update(0.1f);
        scene = outra;
        g_events.clear();
        runtime.Update(0.1f);
        CHECK((g_events == std::vector<std::string>{"x:start", "x:update"}));
        runtime.Stop();
    }

    SECTION("Broadcast chega a todos os scripts iniciados, e só a eles");
    {
        g_events.clear();
        Scene scene;
        Encher(scene, 10);
        scene.CreateEntity("A").Add<Marca>().tag = "a";
        Encher(scene, 10);
        scene.CreateEntity("B").Add<Marca>().tag = "b";
        SceneRuntime runtime(scene);
        runtime.Start();
        g_events.clear();
        runtime.Broadcast({.text = "oi"});
        CHECK((g_events == std::vector<std::string>{"a:msg:oi", "b:msg:oi"}));
        runtime.Stop();
    }

    return TestResult();
}
