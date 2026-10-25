// Passo fixo (Fase 25, Tarefa 5): o relógio dos passos fixos da Application (limite de passos por quadro) e a
// interpolação da física no Update (corpos e personagens entre o penúltimo e o último passo). Sem janela.
#include "TestUtil.h"

#include <motor/app/FixedStepClock.h>
#include <motor/physics/PhysicsSystem.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/scene/Script.h>

#include <cmath>

using namespace motor;

namespace {

constexpr float kDt = 1.0f / 60.0f;

bool Near(double a, double b, double eps = 1e-5) { return std::abs(a - b) < eps; }

// Teleporta a própria entidade no terceiro OnUpdate (depois da interpolação, que roda antes dos scripts).
struct Teleporte : Script {
    int quadros = 0;
    void OnUpdate(float) override {
        if (++quadros == 3) GetEntity().transform.position = {5.0f, 20.0f, 0.0f};
    }
};

// Anda em +X pelo controlador de personagem.
struct Andador : Script {
    void OnFixedUpdate(float) override {
        if (auto* fisica = GetSystem<PhysicsSystem>()) fisica->MoveCharacter(GetId(), {3.0f, 0.0f, 0.0f});
    }
};

Entity& AddFloor(Scene& scene) {
    Entity& floor = scene.CreateEntity("Chao");
    floor.transform.position = {0.0f, -0.5f, 0.0f};
    floor.transform.scale = {40.0f, 1.0f, 40.0f};
    floor.Add<ColliderComponent>();
    return floor;
}

Entity& AddBox(Scene& scene, const glm::vec3& position) {
    Entity& box = scene.CreateEntity("Caixa");
    box.transform.position = position;
    box.Add<ColliderComponent>();
    box.Add<RigidbodyComponent>();
    return box;
}

// Caixa caindo de y = 10 por `steps` passos; a cada passo, Update com o alpha dado. Devolve a altura final (depois
// do último passo, antes de interpolar de novo).
float FallWith(float alpha, int steps) {
    Scene scene;
    AddFloor(scene);
    const EntityId box = AddBox(scene, {0.0f, 10.0f, 0.0f}).id;
    SceneRuntime runtime(scene);
    runtime.Start();
    for (int i = 0; i < steps; ++i) {
        runtime.FixedUpdate(kDt);
        runtime.SetFixedAlpha(alpha);
        runtime.Update(kDt);
    }
    runtime.FixedUpdate(kDt);  // devolve a pose real e anda mais um passo
    const float y = scene.Find(box)->transform.position.y;
    runtime.Stop();
    return y;
}

} // namespace

int main() {
    ComponentRegistry::Instance().Register<Teleporte>("TeleporteDeTeste", "Teleporte de teste");
    ComponentRegistry::Instance().Register<Andador>("AndadorDeTeste", "Andador de teste");
    PhysicsSettings::Global().workerThreads = 0;  // mesma ordem de cálculo em todas as rodadas

    SECTION("Relógio: passos por quadro e alpha");
    {
        FixedStepClock clock(0.01, 4);
        double dt = 0.025;
        CHECK(clock.Advance(dt) == 2);
        CHECK(Near(dt, 0.025));
        CHECK(Near(clock.Alpha(), 0.5));
        dt = 0.006;
        CHECK(clock.Advance(dt) == 1);  // 0,005 que sobrou + 0,006
        CHECK(Near(clock.Alpha(), 0.1, 1e-4));
        dt = 0.0;
        CHECK(clock.Advance(dt) == 0);
    }

    SECTION("Relógio: acima do limite, descarta o excesso e diminui o dt");
    {
        FixedStepClock clock(0.01, 4);
        double dt = 0.0675;  // 6 passos e 0,0075
        CHECK(clock.Advance(dt) == 4);
        CHECK(Near(dt, 0.0475, 1e-9));  // 2 passos (0,02) a menos para o Update
        CHECK(Near(clock.Alpha(), 0.75, 1e-4));  // a fração do passo continua (sem pulo na interpolação)
        dt = 0.0025;
        CHECK(clock.Advance(dt) == 1);
        CHECK(Near(clock.Alpha(), 0.0, 1e-4));

        FixedStepClock semLimite(0.01, 0);
        dt = 0.205;
        CHECK(semLimite.Advance(dt) == 20);
        CHECK(Near(dt, 0.205));
    }

    SECTION("Interpolação: no Update a caixa fica entre os dois últimos passos");
    {
        Scene scene;
        AddFloor(scene);
        const EntityId box = AddBox(scene, {0.0f, 10.0f, 0.0f}).id;
        SceneRuntime runtime(scene);
        runtime.Start();
        for (int i = 0; i < 10; ++i) {  // ganha velocidade
            runtime.FixedUpdate(kDt);
            runtime.Update(kDt);
        }
        const float antes = scene.Find(box)->transform.position.y;
        runtime.FixedUpdate(kDt);
        const float depois = scene.Find(box)->transform.position.y;
        CHECK(depois < antes);

        runtime.SetFixedAlpha(0.25f);
        runtime.Update(kDt);
        CHECK(Near(scene.Find(box)->transform.position.y, antes + 0.25f * (depois - antes)));
        runtime.SetFixedAlpha(0.75f);  // quadro sem passo fixo: interpola de novo, do mesmo par
        runtime.Update(kDt);
        CHECK(Near(scene.Find(box)->transform.position.y, antes + 0.75f * (depois - antes)));
        runtime.SetFixedAlpha(1.0f);
        runtime.Update(kDt);
        CHECK(scene.Find(box)->transform.position.y == depois);
        runtime.Stop();
    }

    SECTION("Interpolação: a simulação não vê a pose interpolada");
    {
        // Mesmo resultado com alpha 0 (o mais longe do último passo) e 1 (sem interpolar).
        const float semInterpolar = FallWith(1.0f, 40);
        const float interpolando = FallWith(0.0f, 40);
        CHECK(semInterpolar < 9.0f);
        CHECK(semInterpolar == interpolando);
    }

    SECTION("Interpolação: script que mexe no Transform no OnUpdate teleporta, como antes");
    {
        Scene scene;
        AddFloor(scene);
        Entity& boxEntity = AddBox(scene, {0.0f, 10.0f, 0.0f});
        boxEntity.Add<Teleporte>();
        const EntityId box = boxEntity.id;
        SceneRuntime runtime(scene);
        runtime.Start();
        for (int i = 0; i < 3; ++i) {
            runtime.FixedUpdate(kDt);
            runtime.SetFixedAlpha(0.5f);
            runtime.Update(kDt);  // no terceiro, o script teleporta para (5, 20, 0)
        }
        CHECK(scene.Find(box)->transform.position == glm::vec3(5.0f, 20.0f, 0.0f));
        runtime.FixedUpdate(kDt);
        const glm::vec3 p = scene.Find(box)->transform.position;
        CHECK(Near(p.x, 5.0f, 1e-3) && p.y > 19.5f && p.y < 20.0f);  // caiu um passo a partir do teleporte
        runtime.Stop();
    }

    SECTION("Interpolação: personagem também");
    {
        Scene scene;
        AddFloor(scene);
        Entity& player = scene.CreateEntity("Jogador");
        player.transform.position = {0.0f, 1.0f, 0.0f};
        player.Add<CharacterControllerComponent>();
        player.Add<Andador>();
        const EntityId id = player.id;
        SceneRuntime runtime(scene);
        runtime.Start();
        for (int i = 0; i < 30; ++i) {
            runtime.FixedUpdate(kDt);
            runtime.Update(kDt);
        }
        const float antes = scene.Find(id)->transform.position.x;
        runtime.FixedUpdate(kDt);
        const float depois = scene.Find(id)->transform.position.x;
        CHECK(depois > antes);
        runtime.SetFixedAlpha(0.5f);
        runtime.Update(kDt);
        CHECK(Near(scene.Find(id)->transform.position.x, 0.5f * (antes + depois)));
        runtime.FixedUpdate(kDt);
        CHECK(Near(scene.Find(id)->transform.position.x - depois, depois - antes, 1e-2));  // velocidade constante
        runtime.Stop();
    }

    SECTION("Interpolação desligada: o Update não mexe nos corpos");
    {
        PhysicsSettings::Global().interpolate = false;
        Scene scene;
        AddFloor(scene);
        const EntityId box = AddBox(scene, {0.0f, 10.0f, 0.0f}).id;
        SceneRuntime runtime(scene);
        runtime.Start();
        runtime.FixedUpdate(kDt);
        runtime.FixedUpdate(kDt);
        const float y = scene.Find(box)->transform.position.y;
        runtime.SetFixedAlpha(0.0f);
        runtime.Update(kDt);
        CHECK(scene.Find(box)->transform.position.y == y);
        runtime.Stop();
        PhysicsSettings::Global().interpolate = true;
    }

    return TestResult();
}
