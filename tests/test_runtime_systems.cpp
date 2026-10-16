// Sistemas do SceneRuntime (RuntimeSystem.h): registro com MOTOR_RUNTIME_SYSTEM, ordem dos ganchos em relação
// aos scripts, estado por runtime e acesso pelos scripts (GetSystem). Os sistemas de exemplo fazem o papel da
// física (antes dos scripts) e do áudio/HUD (depois). Não usa OpenGL nem janela.
#include "TestUtil.h"

#include <motor/scene/Component.h>
#include <motor/scene/RuntimeSystem.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace motor;

namespace {
std::vector<std::string> g_events;

class SomExemplo;

// Faz o papel do mundo da física: roda antes dos scripts e guarda os "corpos" que os scripts registram.
class MundoExemplo : public RuntimeSystem {
public:
    int passos = 0;
    int quadros = 0;
    std::vector<EntityId> corpos;
    bool viuOSomNoStart = false;

    void Adicionar(EntityId id) { corpos.push_back(id); }

    void OnStart(SceneRuntime& runtime) override;
    void OnFixedUpdate(SceneRuntime&, float) override {
        ++passos;
        g_events.push_back("mundo:fixed");
    }
    void OnUpdate(SceneRuntime&, float) override {
        ++quadros;
        g_events.push_back("mundo:update");
    }
    void OnStop(SceneRuntime&) override { g_events.push_back("mundo:stop"); }
};

// Depois dos scripts. Registrado antes do HUD, mas com a mesma ordem: o nome desempata ("Hud" < "Som").
class SomExemplo : public RuntimeSystem {
public:
    void OnStart(SceneRuntime&) override { g_events.push_back("som:start"); }
    void OnFixedUpdate(SceneRuntime&, float) override { g_events.push_back("som:fixed"); }
    void OnUpdate(SceneRuntime&, float) override { g_events.push_back("som:update"); }
    void OnStop(SceneRuntime&) override { g_events.push_back("som:stop"); }
};

class HudExemplo : public RuntimeSystem {
public:
    void OnStart(SceneRuntime&) override { g_events.push_back("hud:start"); }
    void OnFixedUpdate(SceneRuntime&, float) override { g_events.push_back("hud:fixed"); }
    void OnUpdate(SceneRuntime&, float) override { g_events.push_back("hud:update"); }
    void OnStop(SceneRuntime&) override { g_events.push_back("hud:stop"); }
};

// Nunca registrado: GetSystem devolve nullptr.
class NaoRegistrado : public RuntimeSystem {};

void MundoExemplo::OnStart(SceneRuntime& runtime) {
    // Todos os sistemas já existem quando o primeiro OnStart roda.
    viuOSomNoStart = runtime.GetSystem<SomExemplo>() != nullptr;
    g_events.push_back("mundo:start");
}

MOTOR_RUNTIME_SYSTEM(Som, SomExemplo, 10);
MOTOR_RUNTIME_SYSTEM(Hud, HudExemplo, 10);
MOTOR_RUNTIME_SYSTEM(Mundo, MundoExemplo, -10);

// Script de jogador: acha o mundo no OnStart e confere que ele já avançou antes do seu OnFixedUpdate.
struct Jogador : Script {
    MundoExemplo* mundo = nullptr;
    int fixos = 0;
    bool mundoAntesDoScript = true;  // passos do mundo == fixos deste script + 1 em todo OnFixedUpdate
    bool achouNoDestroy = false;
    bool naoRegistradoNulo = false;

    void OnStart() override {
        mundo = GetSystem<MundoExemplo>();
        if (mundo) mundo->Adicionar(GetId());
        naoRegistradoNulo = GetSystem<NaoRegistrado>() == nullptr;
        g_events.push_back("jogador:start");
    }
    void OnFixedUpdate(float) override {
        if (!mundo || mundo->passos != fixos + 1) mundoAntesDoScript = false;
        ++fixos;
        g_events.push_back("jogador:fixed");
    }
    void OnUpdate(float) override { g_events.push_back("jogador:update"); }
    void OnDestroy() override {
        achouNoDestroy = GetSystem<MundoExemplo>() == mundo && mundo != nullptr;
        g_events.push_back("jogador:destroy");
    }
};

using Events = std::vector<std::string>;

size_t IndexOf(const std::vector<RuntimeSystemInfo>& systems, const std::string& name) {
    for (size_t i = 0; i < systems.size(); ++i) {
        if (systems[i].name == name) return i;
    }
    return systems.size();
}
} // namespace

int main() {
    ComponentRegistry::Instance().Register<Jogador>("Jogador", "Jogador");

    SECTION("Registro: ordem pela `order` e, no empate, pelo nome");
    {
        const std::vector<RuntimeSystemInfo> systems = RegisteredRuntimeSystems();
        const size_t mundo = IndexOf(systems, "Mundo");
        const size_t hud = IndexOf(systems, "Hud");
        const size_t som = IndexOf(systems, "Som");
        CHECK(mundo < systems.size() && hud < systems.size() && som < systems.size());
        CHECK(mundo < hud && hud < som);
        CHECK(systems[mundo].order == -10 && *systems[mundo].cppType == typeid(MundoExemplo));
        CHECK(std::is_sorted(systems.begin(), systems.end(), [](const RuntimeSystemInfo& a, const RuntimeSystemInfo& b) {
            return a.order < b.order;
        }));
        CHECK(kSystemOrderPhysics < 0 && kSystemOrderAudio >= 0 && kSystemOrderUi > kSystemOrderAudio);

        // Registrar e tirar na hora (é o que acontece quando uma DLL com sistemas é carregada e descarregada).
        const size_t antes = systems.size();
        RegisterRuntimeSystem({"Temporario", 0, &typeid(NaoRegistrado),
                               []() -> std::unique_ptr<RuntimeSystem> { return std::make_unique<NaoRegistrado>(); }});
        RegisterRuntimeSystem({"Temporario", 5, &typeid(NaoRegistrado),
                               []() -> std::unique_ptr<RuntimeSystem> { return std::make_unique<NaoRegistrado>(); }});
        CHECK(RegisteredRuntimeSystems().size() == antes + 1);  // o mesmo tipo de novo substitui
        UnregisterRuntimeSystem(typeid(NaoRegistrado));
        CHECK(RegisteredRuntimeSystems().size() == antes);
    }

    SECTION("Ordem dos ganchos em relação aos scripts");
    {
        Scene scene;
        Entity& e = scene.CreateEntity("Jogador");
        e.Add<Jogador>();
        const EntityId id = e.id;

        SceneRuntime runtime(scene);
        CHECK(runtime.GetSystem<MundoExemplo>() == nullptr);  // parado: sem sistemas
        g_events.clear();
        runtime.FixedUpdate(0.02f);  // antes do Start: nada
        runtime.Update(0.1f);
        CHECK(g_events.empty());

        runtime.Start();
        CHECK((g_events == Events{"mundo:start", "hud:start", "som:start", "jogador:start"}));

        g_events.clear();
        runtime.FixedUpdate(0.02f);
        CHECK((g_events == Events{"mundo:fixed", "jogador:fixed", "hud:fixed", "som:fixed"}));

        g_events.clear();
        runtime.Update(0.1f);
        CHECK((g_events == Events{"mundo:update", "jogador:update", "hud:update", "som:update"}));

        runtime.FixedUpdate(0.02f);
        runtime.FixedUpdate(0.02f);
        g_events.clear();
        runtime.Stop();
        CHECK((g_events == Events{"jogador:destroy", "som:stop", "hud:stop", "mundo:stop"}));
        CHECK(runtime.GetSystem<MundoExemplo>() == nullptr);

        // Acesso pelos scripts (o componente continua na cena depois do Stop).
        const Jogador* j = scene.Find(id)->Get<Jogador>();
        CHECK(j->fixos == 3);
        CHECK(j->mundoAntesDoScript);
        CHECK(j->achouNoDestroy);
        CHECK(j->naoRegistradoNulo);
    }

    SECTION("Estado próprio por runtime e limpo a cada Start");
    {
        Scene cenaA;
        const EntityId idA = cenaA.CreateEntity("A").id;
        cenaA.Find(idA)->Add<Jogador>();
        Scene cenaB;
        cenaB.CreateEntity("B1").Add<Jogador>();
        cenaB.CreateEntity("B2").Add<Jogador>();

        SceneRuntime a(cenaA);
        SceneRuntime b(cenaB);
        a.Start();
        b.Start();
        MundoExemplo* mundoA = a.GetSystem<MundoExemplo>();
        MundoExemplo* mundoB = b.GetSystem<MundoExemplo>();
        CHECK(mundoA && mundoB && mundoA != mundoB);
        CHECK(mundoA->viuOSomNoStart && mundoB->viuOSomNoStart);
        CHECK(mundoA->corpos == std::vector<EntityId>{idA});  // cada script se registrou no mundo do seu runtime
        CHECK(mundoB->corpos.size() == 2);
        CHECK(cenaA.Find(idA)->Get<Jogador>()->mundo == mundoA);

        a.FixedUpdate(0.02f);
        a.FixedUpdate(0.02f);
        b.FixedUpdate(0.02f);
        b.Update(0.1f);
        CHECK(mundoA->passos == 2 && mundoA->quadros == 0);
        CHECK(mundoB->passos == 1 && mundoB->quadros == 1);

        // Parar e iniciar de novo (novo Play) cria instâncias novas: o estado anterior não vaza.
        a.Stop();
        a.Start();
        mundoA = a.GetSystem<MundoExemplo>();
        CHECK(mundoA && mundoA->passos == 0 && mundoA->corpos.size() == 1);
        CHECK(b.GetSystem<MundoExemplo>()->passos == 1);  // o outro runtime não foi afetado
    }  // destrutores param os runtimes (Stop): sem vazamento nem chamadas depois

    return TestResult();
}
