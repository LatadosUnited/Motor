// Modo Play do editor (PlayMode): copiar a cena ao dar Play, rodar os scripts, pausar e restaurar ao parar.
#include "TestUtil.h"

#include "PlayMode.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/scene/Component.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneSerializer.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <string>

using namespace motor;
using editor::PlayMode;

namespace {
int g_destroyed = 0;

// Cria uma entidade por quadro e anda para a frente.
struct Fabrica : Script {
    float velocidade = 1.0f;
    void OnUpdate(float dt) override {
        GetTransform().position.x += velocidade * dt;
        CreateEntity("Peça");
    }
    void OnDestroy() override { ++g_destroyed; }
};

// Gira em Y (graus por segundo).
struct Girar : Script {
    float velocidade = 90.0f;
    void OnUpdate(float dt) override {
        GetTransform().rotation = glm::angleAxis(glm::radians(velocidade * dt), glm::vec3(0.0f, 1.0f, 0.0f)) *
                                  GetTransform().rotation;
    }
};
} // namespace

int main() {
    ComponentRegistry::Instance().Register<Fabrica>("Fabrica", "Fábrica").Field("velocidade", "Velocidade", &Fabrica::velocidade);
    ComponentRegistry::Instance().Register<Girar>("Girar", "Girar").Field("velocidade", "Velocidade", &Girar::velocidade);

    Scene scene;
    Entity& fabrica = scene.CreateEntity("Fábrica");
    fabrica.Add<Fabrica>();
    const EntityId idFabrica = fabrica.id;
    Entity& piao = scene.CreateEntity("Pião");
    piao.Add<Girar>();
    const EntityId idPiao = piao.id;
    Entity& cam = scene.CreateEntity("Câmera");
    cam.Add<CameraComponent>();
    cam.transform.position = {0.0f, 2.0f, 8.0f};
    const EntityId idCam = cam.id;

    const Scene original = scene;
    AssetLibrary assets;
    const std::string originalJson = SceneSerializer::ToJson(scene, assets);

    SECTION("Parado: nada roda");
    PlayMode play;
    CHECK(!play.IsActive() && !play.IsPaused() && play.Runtime() == nullptr);
    play.Update(1.0f);
    play.FixedUpdate(1.0f);
    CHECK(scene == original);

    SECTION("Play: os scripts mexem na cena");
    EntityId selected = idPiao;
    play.Play(scene, selected);
    CHECK(play.IsActive() && !play.IsPaused() && play.Runtime() && play.Runtime()->IsRunning());
    play.FixedUpdate(1.0f / 60.0f);
    play.Update(0.5f);
    play.Update(0.5f);
    CHECK(scene.Find(idFabrica)->transform.position.x == 1.0f);
    CHECK(scene.Entities().size() == original.Entities().size() + 2);
    CHECK(scene.Find(idPiao)->transform.rotation != original.Find(idPiao)->transform.rotation);
    CHECK(!(scene == original));

    SECTION("Pausa: congela; avançar quadro roda um passo");
    play.SetPaused(true);
    CHECK(play.IsPaused());
    const Scene paused = scene;
    play.Update(0.5f);
    play.FixedUpdate(0.5f);
    CHECK(scene == paused);
    play.Step(0.25f);
    CHECK(scene.Find(idFabrica)->transform.position.x == 1.25f);
    CHECK(play.IsPaused());
    play.SetPaused(false);
    play.Update(0.25f);
    CHECK(scene.Find(idFabrica)->transform.position.x == 1.5f);
    play.Step(1.0f);  // fora da pausa, Step não faz nada
    CHECK(scene.Find(idFabrica)->transform.position.x == 1.5f);

    SECTION("Alterações feitas no Play (como pelo Inspector) também são descartadas");
    scene.Find(idPiao)->name = "Renomeado no Play";
    scene.DestroyEntity(scene.FindByName("Câmera")->id);
    play.Play(scene, selected);  // Play de novo enquanto ativo: ignorado (não troca a cópia guardada)

    SECTION("Parar: OnDestroy e cena restaurada");
    g_destroyed = 0;
    selected = scene.FindByName("Peça")->id;  // selecionou algo que só existe no Play
    play.Stop(scene, selected);
    CHECK(g_destroyed == 1);
    CHECK(!play.IsActive() && !play.IsPaused() && play.Runtime() == nullptr);
    CHECK(scene == original);
    CHECK(SceneSerializer::ToJson(scene, assets) == originalJson);
    CHECK(selected == idPiao);  // a peça sumiu: volta a seleção de antes do Play
    CHECK(!scene.Find(idPiao)->Get<Girar>()->HasStarted());

    SECTION("Seleção que existe na cena restaurada é mantida");
    play.Play(scene, selected);
    EntityId other = idFabrica;
    play.Update(0.1f);
    play.Stop(scene, other);
    CHECK(other == idFabrica);
    CHECK(scene == original);

    SECTION("Ids novos depois do Play continuam únicos");
    const EntityId novo = scene.CreateEntity("Nova").id;
    CHECK(novo != idFabrica && novo != idPiao && novo != idCam);
    CHECK(scene.Find(novo) && scene.Find(novo)->name == "Nova" && scene.Entities().size() == 4);

    play.Stop(scene, selected);  // parar parado: nada
    CHECK(scene.Find(novo) != nullptr);

    return TestResult();
}
