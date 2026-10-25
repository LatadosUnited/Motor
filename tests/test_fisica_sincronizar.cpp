// Sincronização da física com a cena (Fase 25, Tarefa 6): a cada passo, só o que mudou chega aos corpos do Jolt (a
// lista de colliders é refeita só quando a estrutura da cena muda). Aqui ficam os casos em que algo muda sem
// mudar a estrutura (Transform, pai, campo de componente, entidade desativada) e precisa chegar à física.
#include "TestUtil.h"

#include <motor/physics/PhysicsSystem.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>

#include <cmath>

using namespace motor;

namespace {

constexpr float kDt = 1.0f / 60.0f;

void Step(SceneRuntime& runtime, int steps = 1) {
    for (int i = 0; i < steps; ++i) {
        runtime.FixedUpdate(kDt);
        runtime.Update(kDt);
    }
}

// Quem o raio de cima para baixo em (x, 10, z) acerta primeiro, ou kNullEntity.
EntityId HitBelow(SceneRuntime& runtime, float x, float z) {
    auto* fisica = runtime.GetSystem<PhysicsSystem>();
    if (!fisica) return kNullEntity;
    const auto hit = fisica->Raycast({x, 10.0f, z}, {0.0f, -1.0f, 0.0f}, 20.0f);
    return hit ? hit->entity : kNullEntity;
}

} // namespace

int main() {
    SECTION("Estático movido pelo Transform e pelo pai");
    {
        Scene scene;
        Entity& pai = scene.CreateEntity("Pai");
        Entity& bloco = scene.CreateEntity("Bloco", pai.id);
        bloco.Add<ColliderComponent>();
        const EntityId paiId = pai.id, blocoId = bloco.id;
        SceneRuntime runtime(scene);
        runtime.Start();
        Step(runtime);
        CHECK(HitBelow(runtime, 0.0f, 0.0f) == blocoId);

        scene.Find(blocoId)->transform.position = {3.0f, 0.0f, 0.0f};  // o próprio Transform
        Step(runtime);
        CHECK(HitBelow(runtime, 0.0f, 0.0f) == kNullEntity);
        CHECK(HitBelow(runtime, 3.0f, 0.0f) == blocoId);

        scene.Find(paiId)->transform.position = {0.0f, 0.0f, 5.0f};  // só o pai: o mundo do filho muda
        Step(runtime);
        CHECK(HitBelow(runtime, 3.0f, 0.0f) == kNullEntity);
        CHECK(HitBelow(runtime, 3.0f, 5.0f) == blocoId);

        scene.Find(paiId)->transform.scale = {4.0f, 1.0f, 1.0f};  // escala do pai: o corpo é refeito maior
        Step(runtime);
        CHECK(HitBelow(runtime, 12.0f, 5.0f) == blocoId);
        CHECK(HitBelow(runtime, 13.9f, 5.0f) == blocoId);
        runtime.Stop();
    }

    SECTION("Entidade desativada sai da física e volta ao reativar");
    {
        Scene scene;
        Entity& pai = scene.CreateEntity("Grupo");
        Entity& bloco = scene.CreateEntity("Bloco", pai.id);
        bloco.Add<ColliderComponent>();
        const EntityId paiId = pai.id, blocoId = bloco.id;
        SceneRuntime runtime(scene);
        runtime.Start();
        Step(runtime);
        auto* fisica = runtime.GetSystem<PhysicsSystem>();
        CHECK(fisica && fisica->HasBody(blocoId));

        scene.Find(paiId)->active = false;  // desativa pelo pai: a estrutura não muda
        Step(runtime);
        CHECK(!fisica->HasBody(blocoId));
        CHECK(HitBelow(runtime, 0.0f, 0.0f) == kNullEntity);

        scene.Find(paiId)->active = true;
        Step(runtime);
        CHECK(fisica->HasBody(blocoId));
        CHECK(HitBelow(runtime, 0.0f, 0.0f) == blocoId);
        runtime.Stop();
    }

    SECTION("Campo de componente mudado sem mudar a estrutura");
    {
        Scene scene;
        Entity& chao = scene.CreateEntity("Chao");
        chao.transform.position = {0.0f, -0.5f, 0.0f};
        chao.Add<ColliderComponent>().size = {4.0f, 1.0f, 4.0f};
        Entity& caixa = scene.CreateEntity("Caixa");
        caixa.transform.position = {0.0f, 0.5f, 0.0f};
        caixa.Add<ColliderComponent>();
        caixa.Add<RigidbodyComponent>();
        const EntityId chaoId = chao.id, caixaId = caixa.id;
        SceneRuntime runtime(scene);
        runtime.Start();
        Step(runtime, 120);  // a caixa assenta e dorme

        CHECK(HitBelow(runtime, 3.0f, 0.0f) == kNullEntity);
        scene.Find(chaoId)->Get<ColliderComponent>()->size = {10.0f, 1.0f, 10.0f};  // chão estático maior
        Step(runtime);
        CHECK(HitBelow(runtime, 3.0f, 0.0f) == chaoId);

        // Rigidbody: gravidade desligada e um impulso para cima; a caixa sobe e não volta.
        scene.Find(caixaId)->Get<RigidbodyComponent>()->useGravity = false;
        Step(runtime);
        runtime.GetSystem<PhysicsSystem>()->AddImpulse(caixaId, {0.0f, 1.0f, 0.0f});
        Step(runtime, 60);
        CHECK(scene.Find(caixaId)->transform.position.y > 1.3f);

        // Vira cinemático: fica onde o Transform manda.
        scene.Find(caixaId)->Get<RigidbodyComponent>()->isKinematic = true;
        scene.Find(caixaId)->transform.position = {0.0f, 5.0f, 0.0f};
        Step(runtime, 30);
        CHECK(scene.Find(caixaId)->transform.position == glm::vec3(0.0f, 5.0f, 0.0f));
        runtime.Stop();
    }

    SECTION("Componente removido e entidade destruída no meio do Play");
    {
        Scene scene;
        Entity& a = scene.CreateEntity("A");
        a.Add<ColliderComponent>();
        Entity& b = scene.CreateEntity("B");
        b.transform.position = {3.0f, 0.0f, 0.0f};
        b.Add<ColliderComponent>();
        const EntityId aId = a.id, bId = b.id;
        SceneRuntime runtime(scene);
        runtime.Start();
        Step(runtime);
        auto* fisica = runtime.GetSystem<PhysicsSystem>();
        CHECK(fisica->HasBody(aId) && fisica->HasBody(bId));

        scene.Find(aId)->Remove<ColliderComponent>();
        Step(runtime);
        CHECK(!fisica->HasBody(aId) && fisica->HasBody(bId));
        CHECK(HitBelow(runtime, 0.0f, 0.0f) == kNullEntity);

        runtime.Destroy(bId);
        Step(runtime, 2);
        CHECK(!fisica->HasBody(bId));
        CHECK(HitBelow(runtime, 3.0f, 0.0f) == kNullEntity);

        Entity& c = scene.CreateEntity("C");  // novo depois de tudo: ganha corpo no passo seguinte
        c.Add<ColliderComponent>();
        const EntityId cId = c.id;
        Step(runtime);
        CHECK(fisica->HasBody(cId));
        CHECK(HitBelow(runtime, 0.0f, 0.0f) == cId);
        runtime.Stop();
    }

    SECTION("Caixas paradas não são reescritas; a que acorda volta a ser");
    {
        Scene scene;
        Entity& chao = scene.CreateEntity("Chao");
        chao.transform.position = {0.0f, -0.5f, 0.0f};
        chao.transform.scale = {20.0f, 1.0f, 20.0f};
        chao.Add<ColliderComponent>();
        Entity& caixa = scene.CreateEntity("Caixa");
        caixa.transform.position = {0.0f, 0.5f, 0.0f};
        caixa.Add<ColliderComponent>();
        caixa.Add<RigidbodyComponent>();
        const EntityId caixaId = caixa.id;
        SceneRuntime runtime(scene);
        runtime.Start();
        Step(runtime, 180);
        auto* fisica = runtime.GetSystem<PhysicsSystem>();
        CHECK(fisica->Stats().activeBodies == 0);  // dormindo
        const glm::vec3 parada = scene.Find(caixaId)->transform.position;
        Step(runtime, 10);
        CHECK(scene.Find(caixaId)->transform.position == parada);

        fisica->AddImpulse(caixaId, {2.0f, 0.0f, 0.0f});  // acorda
        Step(runtime, 30);
        CHECK(scene.Find(caixaId)->transform.position.x > parada.x + 0.1f);
        runtime.Stop();
    }

    return TestResult();
}
