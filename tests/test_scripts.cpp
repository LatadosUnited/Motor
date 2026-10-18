// Scripts e loop de jogo (SceneRuntime): ciclo de vida, API dos scripts, entrada e câmera. Os scripts são
// definidos aqui mesmo (os de exemplo ficam nos modelos de projeto; ver test_script_project).
// Não usa OpenGL nem janela; o teclado é simulado só no estado interno do Input.
#include "TestUtil.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/core/Input.h>
#include <motor/renderer/Camera.h>
#include <motor/scene/Component.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/scene/SceneSerializer.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace motor;

namespace {
std::vector<std::string> g_events;

bool Has(const std::string& event) {
    return std::find(g_events.begin(), g_events.end(), event) != g_events.end();
}

size_t Count(const std::string& event) {
    return static_cast<size_t>(std::count(g_events.begin(), g_events.end(), event));
}

// Anota cada chamada em g_events com a sua tag.
struct Contador : Script {
    std::string tag = "?";
    int passos = 0;
    void OnStart() override { g_events.push_back(tag + ":start"); }
    void OnUpdate(float) override {
        ++passos;
        g_events.push_back(tag + ":update");
    }
    void OnFixedUpdate(float) override { g_events.push_back(tag + ":fixed"); }
    void OnDestroy() override { g_events.push_back(tag + ":destroy"); }
};

// Usa a API: no 1º quadro cria uma entidade com Contador, no 2º destrói "Alvo", no 3º destrói a si mesmo.
struct Diretor : Script {
    int quadro = 0;
    EntityId criada = kNullEntity;
    void OnUpdate(float) override {
        ++quadro;
        if (quadro == 1) {
            Entity& e = CreateEntity("Criada");
            e.Add<Contador>().tag = "criada";
            criada = e.id;
            Log("criei a entidade {}", e.id);
        } else if (quadro == 2) {
            if (Entity* alvo = FindEntity("Alvo")) Destroy(alvo->id);
        } else if (quadro == 3) {
            DestroySelf();
        }
    }
    void OnDestroy() override { g_events.push_back("diretor:destroy"); }
};

// Anda no eixo X com D/A (testa a entrada vista pelos scripts).
struct Andar : Script {
    float velocidade = 2.0f;
    void OnUpdate(float dt) override {
        if (IsKeyDown(Key::D)) GetTransform().position.x += velocidade * dt;
        if (IsKeyDown(Key::A)) GetTransform().position.x -= velocidade * dt;
    }
};

struct ComponenteSemRegistro {
    int x = 0;
};

// Lança exceção no OnUpdate (como ao chamar Get<T>() de um tipo não registrado).
struct Quebrado : Script {
    int chamadas = 0;
    void OnUpdate(float) override {
        ++chamadas;
        (void)GetEntity().Get<ComponenteSemRegistro>();  // lança via MOTOR_ASSERT
    }
    void OnDestroy() override { g_events.push_back("quebrado:destroy"); }
};

bool Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-3f) {
    return glm::length(a - b) < eps;
}

void Register() {
    auto& registry = ComponentRegistry::Instance();
    registry.Register<Contador>("Contador", "Contador")
        .Field("tag", "Tag", &Contador::tag)
        .Field("passos", "Passos", &Contador::passos);
    registry.Register<Diretor>("Diretor", "Diretor");
    registry.Register<Andar>("Andar", "Andar").Field("velocidade", "Velocidade", &Andar::velocidade);
    registry.Register<Quebrado>("Quebrado", "Quebrado");
}
} // namespace

int main() {
    Register();
    const auto& registry = ComponentRegistry::Instance();

    SECTION("Registro de scripts");
    CHECK(registry.Find("Contador") && registry.Find("Contador")->IsScript());
    CHECK(registry.Find("MeshRenderer") && !registry.Find("MeshRenderer")->IsScript());
    CHECK(registry.Find("Camera") && !registry.Find("Camera")->IsScript());
    CHECK(ComponentRegistry::TypeOf<Contador>() == registry.Find("Contador"));
    CHECK(ComponentRegistry::TypeOf<MeshRenderer>() == registry.Find("MeshRenderer"));

    SECTION("Ciclo de vida: Start, Update, FixedUpdate, Stop");
    {
        g_events.clear();
        Scene scene;
        Entity& a = scene.CreateEntity("A");
        a.Add<Contador>().tag = "A";
        Entity& b = scene.CreateEntity("B");
        b.Add<Contador>().tag = "B";
        b.active = false;
        const EntityId idB = b.id;
        Entity& filho = scene.CreateEntity("Filho de B", idB);  // inativo por causa do pai
        filho.Add<Contador>().tag = "F";

        CHECK(!scene.Find(a.id)->Get<Contador>()->HasStarted());
        SceneRuntime runtime(scene);
        runtime.Update(0.1f);  // antes do Start: nada
        CHECK(g_events.empty());

        runtime.Start();
        CHECK(runtime.IsRunning());
        CHECK(g_events == std::vector<std::string>{"A:start"});
        runtime.FixedUpdate(0.02f);
        runtime.Update(0.1f);
        CHECK((g_events == std::vector<std::string>{"A:start", "A:fixed", "A:update"}));
        CHECK(scene.Entities()[0]->Get<Contador>()->passos == 1);

        scene.Find(idB)->active = true;  // ativar: inicia no próximo passo, ele e o filho
        runtime.Update(0.1f);
        CHECK(Has("B:start") && Has("F:start") && Has("B:update") && Has("F:update"));
        CHECK(Count("A:start") == 1);

        scene.Find(idB)->active = false;  // desativado: não atualiza, mas recebe OnDestroy ao parar
        g_events.clear();
        runtime.Update(0.1f);
        CHECK(Has("A:update") && !Has("B:update") && !Has("F:update"));

        g_events.clear();
        runtime.Stop();
        CHECK(!runtime.IsRunning());
        CHECK(Has("A:destroy") && Has("B:destroy") && Has("F:destroy") && g_events.size() == 3);
        g_events.clear();
        runtime.Update(0.1f);
        runtime.Stop();
        CHECK(g_events.empty());
    }

    SECTION("Cópia de um script em execução começa sem dono e não iniciada");
    {
        Scene scene;
        Entity& a = scene.CreateEntity("A");
        a.Add<Contador>().tag = "copia";
        SceneRuntime runtime(scene);
        runtime.Start();
        Scene copy = scene;
        CHECK(scene.Entities()[0]->Get<Contador>()->HasStarted());
        CHECK(!copy.Entities()[0]->Get<Contador>()->HasStarted());
        CHECK(copy.Entities()[0]->Get<Contador>()->tag == "copia");
        CHECK(copy == scene);
    }

    SECTION("API: criar, achar por nome, destruir (adiado, com filhos) e destruir a si mesmo");
    {
        g_events.clear();
        Scene scene;
        const EntityId diretor = scene.CreateEntity("Diretor").id;
        scene.Find(diretor)->Add<Diretor>();
        const EntityId alvo = scene.CreateEntity("Alvo").id;
        const EntityId filho = scene.CreateEntity("Filho", alvo).id;
        scene.Find(filho)->Add<Contador>().tag = "filho";

        SceneRuntime runtime(scene);
        runtime.Start();
        runtime.Update(0.016f);  // quadro 1: cria
        const EntityId criada = scene.Find(diretor)->Get<Diretor>()->criada;
        CHECK(scene.Find(criada) && scene.Find(criada)->name == "Criada");
        CHECK(!Has("criada:start"));  // criada durante o quadro: começa no próximo
        CHECK(scene.FindByName("Criada") == scene.Find(criada));

        runtime.Update(0.016f);  // quadro 2: destrói Alvo (e o filho) no fim do quadro
        CHECK(Has("criada:start") && Has("criada:update"));
        CHECK(!scene.Find(alvo) && !scene.Find(filho));
        CHECK(Has("filho:destroy"));

        runtime.Update(0.016f);  // quadro 3: destrói a si mesmo
        CHECK(!scene.Find(diretor) && Has("diretor:destroy"));
        CHECK(scene.Find(criada) != nullptr);

        runtime.Destroy(12345);  // id inexistente: ignorado
        runtime.Update(0.016f);
        g_events.clear();
        runtime.Stop();
        CHECK(g_events == std::vector<std::string>{"criada:destroy"});
    }

    SECTION("Tempo do jogo");
    {
        Scene scene;
        SceneRuntime runtime(scene);
        runtime.Start();
        runtime.Update(0.25f);
        runtime.Update(0.5f);
        runtime.FixedUpdate(0.02f);
        CHECK(runtime.Time().frame == 2);
        CHECK(std::abs(runtime.Time().elapsed - 0.75) < 1e-6);
        CHECK(runtime.Time().delta == 0.5f && runtime.Time().fixedDelta == 0.02f);
    }

    SECTION("Entrada nos scripts (teclas simuladas só no Input interno)");
    {
        Scene scene;
        Entity& e = scene.CreateEntity("Jogador");
        e.Add<Andar>();
        const EntityId id = e.id;
        SceneRuntime runtime(scene);
        runtime.Start();
        runtime.Update(0.5f);
        CHECK(Near(scene.Find(id)->transform.position, glm::vec3(0.0f)));  // nada apertado

        Input::SetKeyState(Key::D, true);
        runtime.Update(0.5f);
        CHECK(Near(scene.Find(id)->transform.position, {1.0f, 0.0f, 0.0f}));

        runtime.SetInputEnabled(false);  // jogo sem foco: teclas ignoradas
        runtime.Update(0.5f);
        CHECK(Near(scene.Find(id)->transform.position, {1.0f, 0.0f, 0.0f}));
        Input::SetKeyState(Key::D, false);
    }

    SECTION("Câmera principal");
    {
        Scene scene;
        CHECK(scene.FindMainCamera() == kNullEntity);
        Entity& secundaria = scene.CreateEntity("Secundária");
        secundaria.Add<CameraComponent>().main = false;
        const EntityId idSec = secundaria.id;
        CHECK(scene.FindMainCamera() == idSec);  // sem principal: a primeira com Câmera

        Entity& principal = scene.CreateEntity("Principal");
        CameraComponent& cc = principal.Add<CameraComponent>();
        cc.fov = 45.0f;
        principal.transform.position = {0.0f, 2.0f, 5.0f};
        principal.transform.SetEulerDegrees({0.0f, 90.0f, 0.0f});  // -Z gira para -X
        const EntityId idPri = principal.id;
        CHECK(scene.FindMainCamera() == idPri);

        Camera camera;
        CHECK(scene.ApplyCamera(idPri, camera));
        CHECK(Near(camera.position, {0.0f, 2.0f, 5.0f}));
        CHECK(Near(camera.Forward(), {-1.0f, 0.0f, 0.0f}));
        CHECK(std::abs(camera.roll) < 1e-3f);
        CHECK(camera.fovY == 45.0f);
        CHECK(!scene.ApplyCamera(kNullEntity, camera));

        // Roll: inclinar a entidade da câmera em torno do próprio eixo gira Right() e Up() junto.
        principal.transform.SetEulerDegrees({15.0f, 45.0f, -35.0f});
        CHECK(scene.ApplyCamera(idPri, camera));
        const glm::mat4 world = scene.WorldMatrix(idPri);
        CHECK(Near(camera.Forward(), glm::normalize(-glm::vec3(world[2]))));
        CHECK(Near(camera.Right(), glm::normalize(glm::vec3(world[0]))));
        CHECK(Near(camera.Up(), glm::normalize(glm::vec3(world[1]))));

        scene.Find(idPri)->active = false;  // inativa não conta
        CHECK(scene.FindMainCamera() == idSec);
    }

    SECTION("Scripts e câmera são salvos e carregados como componentes");
    {
        Scene scene;
        Entity& e = scene.CreateEntity("Pião");
        Contador& c = e.Add<Contador>();
        c.tag = "pião";
        c.passos = -45;
        e.Add<CameraComponent>().farPlane = 80.0f;
        AssetLibrary assets;
        const std::string text = SceneSerializer::ToJson(scene, assets);
        CHECK(text.find("\"type\": \"Contador\"") != std::string::npos);
        Scene loaded;
        std::string error;
        CHECK(SceneSerializer::FromJson(text, loaded, assets, &error));
        CHECK(loaded == scene);
        const Contador* lc = loaded.Entities().empty() ? nullptr : loaded.Entities()[0]->Get<Contador>();
        CHECK(lc && lc->tag == "pião" && lc->passos == -45 && !lc->HasStarted());
    }

    SECTION("Exceção em script não derruba o runtime e desativa só o script que falhou");
    {
        g_events.clear();
        Scene scene;
        Entity& ruim = scene.CreateEntity("Bomba");
        Quebrado& q = ruim.Add<Quebrado>();
        Entity& boa = scene.CreateEntity("Relógio");
        Contador& c = boa.Add<Contador>();
        c.tag = "ok";

        SceneRuntime runtime(scene);
        runtime.Start();
        CHECK(!runtime.HasError() && runtime.ErrorCount() == 0);

        runtime.Update(0.016f);
        CHECK(runtime.HasError());
        CHECK(runtime.TakeError());
        CHECK(!runtime.TakeError());  // já consumido
        CHECK(runtime.ErrorCount() == 1);
        CHECK(runtime.LastError().find("Quebrado::OnUpdate") != std::string::npos);
        CHECK(runtime.LastError().find("'Bomba'") != std::string::npos);
        CHECK(q.chamadas == 1);
        CHECK(c.passos == 1);

        // Nos próximos quadros o script falho não roda mais, mas o resto do jogo continua.
        runtime.Update(0.016f);
        runtime.FixedUpdate(0.016f);
        CHECK(!runtime.TakeError());
        CHECK(runtime.ErrorCount() == 1);
        CHECK(q.chamadas == 1);
        CHECK(c.passos == 2);

        runtime.Stop();
        CHECK(!Has("quebrado:destroy"));
        CHECK(Has("ok:destroy"));
    }

    return TestResult();
}
