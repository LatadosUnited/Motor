// Física (Fase 5): componentes Collider/Rigidbody/CharacterController, o PhysicsSystem (Jolt) rodando dentro do
// SceneRuntime, eventos de colisão e trigger nos scripts, raios, personagem e o contorno dos colliders do
// editor. Não usa OpenGL nem janela.
#include "TestUtil.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/assets/AssetLoaders.h>
#include <motor/physics/ColliderGizmo.h>
#include <motor/physics/PhysicsSystem.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/scene/SceneSerializer.h>
#include <motor/scene/Script.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace motor;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// Anota os eventos da física que recebe.
struct Sensor : Script {
    int collisionEnters = 0, collisionExits = 0, triggerEnters = 0, triggerExits = 0;
    EntityId lastOther = kNullEntity;
    Collision lastCollision;
    void OnCollisionEnter(const Collision& c) override {
        ++collisionEnters;
        lastOther = c.other;
        lastCollision = c;
    }
    void OnCollisionExit(EntityId other) override {
        ++collisionExits;
        lastOther = other;
    }
    void OnTriggerEnter(EntityId other) override {
        ++triggerEnters;
        lastOther = other;
    }
    void OnTriggerExit(EntityId other) override {
        ++triggerExits;
        lastOther = other;
    }
};

// Jogador: anda em +X e pula quando pedem, pelo PhysicsSystem.
struct Jogador : Script {
    float velocidade = 3.0f;
    bool pular = false;
    bool pulou = false;
    void OnFixedUpdate(float) override {
        auto* fisica = GetSystem<PhysicsSystem>();
        if (!fisica) return;
        fisica->MoveCharacter(GetId(), glm::vec3(velocidade, 0.0f, 0.0f));
        if (pular && fisica->Jump(GetId(), 5.0f)) {
            pular = false;
            pulou = true;
        }
    }
};

struct SensorQuebrado : Script {
    void OnTriggerEnter(EntityId) override { throw std::runtime_error("erro no trigger"); }
};

void Register() {
    auto& registry = ComponentRegistry::Instance();
    registry.Register<Sensor>("SensorDeTeste", "Sensor de teste");
    registry.Register<SensorQuebrado>("SensorQuebradoDeTeste", "Sensor quebrado de teste");
    registry.Register<Jogador>("JogadorDeTeste", "Jogador de teste").Field("velocidade", "Velocidade", &Jogador::velocidade);
}

// Chão estático: caixa de 20 x 1 x 20 com o topo em y = 0.
Entity& AddFloor(Scene& scene) {
    Entity& floor = scene.CreateEntity("Chao");
    floor.transform.position = {0.0f, -0.5f, 0.0f};
    floor.transform.scale = {20.0f, 1.0f, 20.0f};
    floor.Add<ColliderComponent>();
    return floor;
}

Entity& AddBox(Scene& scene, const glm::vec3& position, const char* name = "Caixa") {
    Entity& box = scene.CreateEntity(name);
    box.transform.position = position;
    box.Add<ColliderComponent>();
    box.Add<RigidbodyComponent>();
    return box;
}

void Run(SceneRuntime& runtime, float seconds) {
    const int steps = static_cast<int>(std::lround(seconds / kDt));
    for (int i = 0; i < steps; ++i) {
        runtime.FixedUpdate(kDt);
        runtime.Update(kDt);
    }
}

bool Near(float a, float b, float eps) { return std::abs(a - b) < eps; }

} // namespace

int main() {
    Register();
    const auto& registry = ComponentRegistry::Instance();

    SECTION("Componentes registrados e salvos na cena");
    CHECK(registry.Find("Collider") && registry.Find("Rigidbody") && registry.Find("CharacterController"));
    CHECK(!RegisteredRuntimeSystems().empty());
    {
        Scene scene;
        Entity& e = scene.CreateEntity("Bola");
        auto& c = e.Add<ColliderComponent>();
        c.shape = ColliderShape::Capsule;
        c.radius = 0.25f;
        c.isTrigger = true;
        auto& rb = e.Add<RigidbodyComponent>();
        rb.mass = 3.0f;
        rb.isKinematic = true;
        e.Add<CharacterControllerComponent>().stepHeight = 0.5f;

        AssetLibrary assets;
        const std::string json = SceneSerializer::ToJson(scene, assets);
        CHECK(json.find("\"Capsule\"") != std::string::npos);  // forma gravada como texto
        Scene loaded;
        CHECK(SceneSerializer::FromJson(json, loaded, assets));
        CHECK(loaded == scene);
        const Entity* l = loaded.FindByName("Bola");
        CHECK(l && l->Get<ColliderComponent>()->shape == ColliderShape::Capsule);
        CHECK(l && l->Get<RigidbodyComponent>()->isKinematic && l->Get<RigidbodyComponent>()->mass == 3.0f);
        CHECK(l && l->Get<CharacterControllerComponent>()->stepHeight == 0.5f);

        // Leitura tolerante: número também vale; texto desconhecido mantém o valor.
        const auto* field = &registry.Find("Collider")->fields[0];
        ColliderShape shape = ColliderShape::Box;
        field->custom->fromJson("2", &shape, assets);
        CHECK(shape == ColliderShape::Capsule);
        field->custom->fromJson("\"Triangulo\"", &shape, assets);
        CHECK(shape == ColliderShape::Capsule);
    }

    SECTION("Cena sem física não cria mundo");
    {
        Scene scene;
        scene.CreateEntity("Vazia");
        SceneRuntime runtime(scene);
        runtime.Start();
        auto* fisica = runtime.GetSystem<PhysicsSystem>();
        CHECK(fisica != nullptr);
        Run(runtime, 0.1f);
        CHECK(fisica && fisica->Stats().bodies == 0 && !fisica->Raycast({0, 1, 0}, {0, -1, 0}));
        runtime.Stop();
    }

    SECTION("Caixa cai e para em cima do chão");
    {
        Scene scene;
        const EntityId floor = AddFloor(scene).id;
        Entity& box = AddBox(scene, {0.0f, 5.0f, 0.0f});
        const EntityId boxId = box.id;
        Sensor& sensor = box.Add<Sensor>();
        SceneRuntime runtime(scene);
        runtime.Start();
        auto* fisica = runtime.GetSystem<PhysicsSystem>();
        CHECK(fisica && fisica->HasBody(boxId) && fisica->HasBody(floor));
        Run(runtime, 0.5f);
        const float midAir = scene.Find(boxId)->transform.position.y;
        CHECK(midAir < 5.0f && midAir > 0.5f);  // caindo
        Run(runtime, 3.0f);
        const glm::vec3 p = scene.Find(boxId)->transform.position;
        std::printf("  caixa parou em (%.3f, %.3f, %.3f)\n", p.x, p.y, p.z);
        CHECK(Near(p.y, 0.5f, 0.03f) && Near(p.x, 0.0f, 0.01f) && Near(p.z, 0.0f, 0.01f));
        CHECK(glm::length(fisica->GetVelocity(boxId)) < 0.05f);
        CHECK(sensor.collisionEnters == 1 && sensor.lastOther == floor);
        CHECK(sensor.lastCollision.normal.y > 0.9f);  // o chão empurra a caixa para cima
        CHECK(sensor.lastCollision.relativeSpeed > 5.0f);  // caiu de ~4.5 m: ~9 m/s
        CHECK(sensor.collisionExits == 0);
        runtime.Stop();
        CHECK(runtime.GetSystem<PhysicsSystem>() == nullptr);  // parado: o sistema foi destruído
    }

    SECTION("Pilha de caixas fica de pé");
    {
        Scene scene;
        AddFloor(scene);
        std::vector<EntityId> boxes;
        for (int i = 0; i < 10; ++i) boxes.push_back(AddBox(scene, {0.0f, 0.5f + 1.02f * float(i), 0.0f}).id);
        SceneRuntime runtime(scene);
        runtime.Start();
        Run(runtime, 5.0f);
        bool stacked = true;
        for (int i = 0; i < 10; ++i) {
            const glm::vec3 p = scene.Find(boxes[i])->transform.position;
            if (!Near(p.y, 0.5f + float(i), 0.05f) || !Near(p.x, 0.0f, 0.05f) || !Near(p.z, 0.0f, 0.05f)) {
                std::printf("  caixa %d fora do lugar: (%.3f, %.3f, %.3f)\n", i, p.x, p.y, p.z);
                stacked = false;
            }
        }
        CHECK(stacked);
        CHECK(runtime.GetSystem<PhysicsSystem>()->Stats().activeBodies == 0);  // tudo dormindo
        runtime.Stop();
    }

    SECTION("Rigidbody: sem gravidade, cinemático, impulso, escala e hierarquia");
    {
        Scene scene;
        AddFloor(scene);
        Entity& floating = AddBox(scene, {-3.0f, 2.0f, 0.0f}, "Flutua");
        floating.Get<RigidbodyComponent>()->useGravity = false;
        Entity& kinematic = AddBox(scene, {3.0f, 2.0f, 0.0f}, "Cinematico");
        kinematic.Get<RigidbodyComponent>()->isKinematic = true;
        Entity& pushed = AddBox(scene, {0.0f, 0.5f, 5.0f}, "Empurrada");
        Entity& big = AddBox(scene, {6.0f, 3.0f, 0.0f}, "Grande");
        big.transform.scale = glm::vec3(2.0f);  // caixa 2 x 2 x 2: para com o centro em y = 1
        Entity& parent = scene.CreateEntity("Pai");
        parent.transform.position = {-6.0f, 0.0f, 0.0f};
        const EntityId child = AddBox(scene, {0.0f, 3.0f, 0.0f}, "Filha").id;
        scene.SetParent(child, parent.id, false);
        const EntityId ids[] = {floating.id, kinematic.id, pushed.id, big.id};

        SceneRuntime runtime(scene);
        runtime.Start();
        auto* fisica = runtime.GetSystem<PhysicsSystem>();
        CHECK(fisica->AddImpulse(ids[2], {0.0f, 0.0f, 4.0f}));  // 1 kg: 4 m/s para +Z
        CHECK(!fisica->AddImpulse(ids[1], {0.0f, 10.0f, 0.0f}));  // cinemático não recebe
        Run(runtime, 3.0f);
        CHECK(Near(scene.Find(ids[0])->transform.position.y, 2.0f, 1e-3f));
        CHECK(Near(scene.Find(ids[1])->transform.position.y, 2.0f, 1e-6f));
        CHECK(scene.Find(ids[2])->transform.position.z > 5.5f);  // escorregou e o atrito parou
        CHECK(glm::length(fisica->GetVelocity(ids[2])) < 0.05f);
        CHECK(Near(scene.Find(ids[3])->transform.position.y, 1.0f, 0.03f));
        CHECK(scene.Find(ids[3])->transform.scale == glm::vec3(2.0f));  // a física não mexe na escala
        // Filha: posição local relativa ao pai, que está em x = -6.
        const glm::vec3 local = scene.Find(child)->transform.position;
        const glm::vec3 world = glm::vec3(scene.WorldMatrix(child)[3]);
        CHECK(Near(local.x, 0.0f, 0.01f) && Near(world.x, -6.0f, 0.01f) && Near(world.y, 0.5f, 0.03f));

        // Cinemático movido pelo Transform empurra a caixa que estava parada no caminho.
        Entity* k = scene.Find(ids[1]);
        k->transform.position = {3.0f, 0.5f, 0.0f};
        Run(runtime, 0.1f);
        for (int i = 0; i < 60; ++i) {
            scene.Find(ids[1])->transform.position.x += 0.05f;  // 3 m/s em direção à caixa grande
            runtime.FixedUpdate(kDt);
        }
        CHECK(scene.Find(ids[3])->transform.position.x > 6.2f);
        runtime.Stop();
    }

    SECTION("Teleporte pelo Transform, componente alterado e entidade destruída");
    {
        Scene scene;
        const EntityId floor = AddFloor(scene).id;
        Entity& box = AddBox(scene, {0.0f, 0.5f, 0.0f});
        const EntityId boxId = box.id;
        Entity& other = AddBox(scene, {0.0f, 1.5f, 0.0f}, "EmCima");
        const EntityId otherId = other.id;
        Sensor& floorSensor = scene.Find(floor)->Add<Sensor>();
        SceneRuntime runtime(scene);
        runtime.Start();
        Run(runtime, 1.0f);
        CHECK(floorSensor.collisionEnters == 1);

        scene.Find(boxId)->transform.position = {5.0f, 3.0f, 0.0f};  // teleporte
        Run(runtime, 2.0f);
        glm::vec3 p = scene.Find(boxId)->transform.position;
        CHECK(Near(p.x, 5.0f, 0.05f) && Near(p.y, 0.5f, 0.03f));
        CHECK(Near(scene.Find(otherId)->transform.position.y, 0.5f, 0.03f));  // a de cima caiu no chão

        scene.Find(boxId)->Get<ColliderComponent>()->size = glm::vec3(2.0f);  // refaz o corpo maior
        Run(runtime, 2.0f);
        p = scene.Find(boxId)->transform.position;
        CHECK(Near(p.y, 1.0f, 0.03f));

        const int exitsBefore = floorSensor.collisionExits;
        scene.DestroyEntity(boxId);  // como a exclusão no editor durante o Play
        Run(runtime, 0.1f);
        CHECK(!runtime.GetSystem<PhysicsSystem>()->HasBody(boxId));
        CHECK(floorSensor.collisionExits == exitsBefore + 1 && floorSensor.lastOther == boxId);
        runtime.Stop();
    }

    SECTION("Trigger: quem entra e quem sai");
    {
        Scene scene;
        AddFloor(scene);
        Entity& zone = scene.CreateEntity("Zona");
        zone.transform.position = {0.0f, 3.0f, 0.0f};
        zone.Add<ColliderComponent>().isTrigger = true;
        Sensor& zoneSensor = zone.Add<Sensor>();
        const EntityId zoneId = zone.id;
        Entity& ball = AddBox(scene, {0.0f, 6.0f, 0.0f}, "Bola");
        ball.Get<ColliderComponent>()->shape = ColliderShape::Sphere;
        ball.Get<ColliderComponent>()->radius = 0.25f;
        Sensor& ballSensor = ball.Add<Sensor>();
        const EntityId ballId = ball.id;
        SceneRuntime runtime(scene);
        runtime.Start();
        Run(runtime, 3.0f);
        CHECK(zoneSensor.triggerEnters == 1 && zoneSensor.triggerExits == 1 && zoneSensor.lastOther == ballId);
        CHECK(ballSensor.triggerEnters == 1 && ballSensor.triggerExits == 1);
        CHECK(zoneSensor.collisionEnters == 0);
        CHECK(Near(scene.Find(ballId)->transform.position.y, 0.25f, 0.03f));  // atravessou o trigger e parou no chão
        // Raios não veem triggers.
        auto hit = runtime.GetSystem<PhysicsSystem>()->Raycast({0.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f});
        CHECK(hit && hit->entity == ballId && hit->entity != zoneId);
        runtime.Stop();
    }

    SECTION("Raycast");
    {
        Scene scene;
        const EntityId floor = AddFloor(scene).id;
        const EntityId box = AddBox(scene, {2.0f, 0.5f, 0.0f}).id;
        SceneRuntime runtime(scene);
        runtime.Start();  // corpos já existem no Start (antes do primeiro passo)
        auto* fisica = runtime.GetSystem<PhysicsSystem>();
        auto hit = fisica->Raycast({0.0f, 5.0f, 0.0f}, {0.0f, -2.0f, 0.0f});
        CHECK(hit && hit->entity == floor && Near(hit->distance, 5.0f, 1e-3f) && Near(hit->normal.y, 1.0f, 1e-3f));
        CHECK(hit && Near(hit->point.y, 0.0f, 1e-3f));
        hit = fisica->Raycast({-5.0f, 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f});
        CHECK(hit && hit->entity == box && Near(hit->point.x, 1.5f, 1e-3f) && Near(hit->normal.x, -1.0f, 1e-3f));
        hit = fisica->Raycast({-5.0f, 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, 3.0f);
        CHECK(!hit);  // curto demais
        hit = fisica->Raycast({2.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f, box);
        CHECK(hit && hit->entity == floor);  // atravessa a entidade ignorada
        runtime.Stop();
    }

    SECTION("Personagem anda, pula, não atravessa o chão e empurra caixas");
    {
        Scene scene;
        AddFloor(scene);
        Entity& player = scene.CreateEntity("Jogador");
        player.transform.position = {0.0f, 3.0f, 0.0f};
        player.Add<CharacterControllerComponent>();  // 1.8 m: centro em y = 0.9 no chão
        Jogador& jogador = player.Add<Jogador>();
        jogador.velocidade = 0.0f;
        const EntityId playerId = player.id;
        Entity& trigger = scene.CreateEntity("Chegada");
        trigger.transform.position = {6.0f, 1.0f, 0.0f};
        trigger.Add<ColliderComponent>().isTrigger = true;
        Sensor& chegada = trigger.Add<Sensor>();

        SceneRuntime runtime(scene);
        runtime.Start();
        auto* fisica = runtime.GetSystem<PhysicsSystem>();
        Run(runtime, 2.0f);  // cai de 3 m e para no chão
        glm::vec3 p = scene.Find(playerId)->transform.position;
        std::printf("  personagem no chão em y = %.3f\n", p.y);
        CHECK(Near(p.y, 0.9f, 0.05f) && fisica->IsGrounded(playerId));

        jogador.velocidade = 3.0f;
        Run(runtime, 2.0f);
        p = scene.Find(playerId)->transform.position;
        std::printf("  andou até x = %.3f\n", p.x);
        CHECK(Near(p.x, 6.0f, 0.15f) && Near(p.y, 0.9f, 0.05f));
        CHECK(chegada.triggerEnters == 1 && chegada.lastOther == playerId);  // o trigger vê o personagem

        jogador.velocidade = 0.0f;
        jogador.pular = true;
        float highest = 0.0f;
        for (int i = 0; i < 30; ++i) {
            runtime.FixedUpdate(kDt);
            highest = std::max(highest, scene.Find(playerId)->transform.position.y);
        }
        std::printf("  pulo chegou a y = %.3f\n", highest);
        CHECK(jogador.pulou && highest > 1.9f);  // 5 m/s: sobe ~1.27 m
        CHECK(!fisica->Jump(playerId, 5.0f));    // no ar não pula
        Run(runtime, 2.0f);
        CHECK(Near(scene.Find(playerId)->transform.position.y, 0.9f, 0.05f) && fisica->IsGrounded(playerId));

        // Queda alta e rápida não atravessa o chão, e teleportar no meio da queda zera a velocidade.
        scene.Find(playerId)->transform.position = {0.0f, 40.0f, 0.0f};
        jogador.velocidade = 4.0f;
        Run(runtime, 1.0f);  // caindo rápido (~-9,8 m/s) e andando em +X
        CHECK(fisica->GetCharacterVelocity(playerId).y < -5.0f);
        jogador.velocidade = 0.0f;
        fisica->MoveCharacter(playerId, {6.0f, 0.0f, 0.0f});  // velocidade guardada antes do teleporte
        scene.Find(playerId)->transform.position = {0.0f, 20.0f, 0.0f};
        runtime.FixedUpdate(kDt);  // um único passo após o teleporte: só a gravidade deste passo
        const glm::vec3 velAfterTeleport = fisica->GetCharacterVelocity(playerId);
        CHECK(velAfterTeleport.y > -0.5f && Near(velAfterTeleport.x, 0.0f, 1e-4f));
        CHECK(scene.Find(playerId)->transform.position.y > 19.9f);
        Run(runtime, 4.0f);
        CHECK(Near(scene.Find(playerId)->transform.position.y, 0.9f, 0.05f));

        // Caixa no caminho é empurrada.
        const EntityId box = AddBox(scene, {1.5f, 0.5f, 0.0f}).id;
        Run(runtime, 0.5f);
        jogador.velocidade = 2.0f;
        Run(runtime, 2.0f);
        CHECK(scene.Find(box)->transform.position.x > 2.0f);
        runtime.Stop();
    }

    SECTION("Collider de malha (fonte de malhas na CPU)");
    {
        // A Mesh real precisa de OpenGL; aqui o ponteiro só identifica a malha para a fonte de dados.
        alignas(16) static unsigned char fakeStorage[256] = {};
        std::shared_ptr<Mesh> fakeMesh(reinterpret_cast<Mesh*>(fakeStorage), [](Mesh*) {});
        MeshData ramp;  // rampa de 10 x 10 subindo 2 m em +X (dois triângulos)
        ramp.vertices = {{{-5.0f, 0.0f, -5.0f}, {}, {}}, {{5.0f, 2.0f, -5.0f}, {}, {}},
                         {{5.0f, 2.0f, 5.0f}, {}, {}}, {{-5.0f, 0.0f, 5.0f}, {}, {}}};
        ramp.indices = {0, 2, 1, 0, 3, 2};
        int calls = 0;
        PhysicsSettings& settings = PhysicsSettings::Global();
        const PhysicsSettings saved = settings;
        settings.meshData = [&](const Mesh& mesh, MeshData& out) {
            ++calls;
            if (&mesh != fakeMesh.get()) return false;
            out = ramp;
            return true;
        };
        settings.workerThreads = 0;  // também confere o job system sem threads extras

        Scene scene;
        Entity& ground = scene.CreateEntity("Rampa");
        ground.Add<MeshRenderer>().mesh = fakeMesh;
        ground.Add<ColliderComponent>().shape = ColliderShape::Mesh;
        Entity& second = scene.CreateEntity("Rampa2");  // a mesma malha: dados lidos uma vez
        second.transform.position = {0.0f, -10.0f, 0.0f};
        second.Add<MeshRenderer>().mesh = fakeMesh;
        second.Add<ColliderComponent>().shape = ColliderShape::Mesh;
        const EntityId ball = AddBox(scene, {0.0f, 4.0f, 0.0f}, "Bola").id;
        scene.Find(ball)->Get<ColliderComponent>()->shape = ColliderShape::Sphere;

        SceneRuntime runtime(scene);
        runtime.Start();
        auto* fisica = runtime.GetSystem<PhysicsSystem>();
        auto hit = fisica->Raycast({0.0f, 10.0f, 2.0f}, {0.0f, -1.0f, 0.0f});
        CHECK(hit && hit->entity == ground.id && Near(hit->point.y, 1.0f, 1e-3f));  // meio da rampa
        CHECK(hit && hit->normal.x < -0.1f && hit->normal.y > 0.9f);
        Run(runtime, 1.0f);
        CHECK(fisica->Stats().threads == 0);
        CHECK(calls == 1);
        Run(runtime, 2.0f);
        CHECK(scene.Find(ball)->transform.position.x < -1.0f);  // rolou rampa abaixo (-X)
        CHECK(scene.Find(ball)->transform.position.y > -1.0f);  // em cima da rampa, não atravessou
        runtime.Stop();
        settings = saved;
    }

    SECTION("Contorno dos colliders para o editor");
    {
        std::vector<GizmoLine> lines;
        ColliderComponent box;
        box.size = {2.0f, 1.0f, 4.0f};
        glm::mat4 world = Transform{{10.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 1.0f}}.Matrix();
        AppendColliderLines(box, world, lines);
        CHECK(lines.size() == 12);
        glm::vec3 lo(1e9f), hi(-1e9f);
        for (const GizmoLine& l : lines) {
            lo = glm::min(lo, glm::min(l.a, l.b));
            hi = glm::max(hi, glm::max(l.a, l.b));
        }
        CHECK(Near(lo.x, 9.0f, 1e-4f) && Near(hi.x, 11.0f, 1e-4f) && Near(lo.y, -1.0f, 1e-4f) && Near(hi.z, 2.0f, 1e-4f));

        lines.clear();
        CharacterControllerComponent controller;  // 1.8 m, raio 0.3
        AppendCharacterLines(controller, Transform{{0.0f, 5.0f, 0.0f}}.Matrix(), lines);
        lo = glm::vec3(1e9f);
        hi = glm::vec3(-1e9f);
        for (const GizmoLine& l : lines) {
            lo = glm::min(lo, glm::min(l.a, l.b));
            hi = glm::max(hi, glm::max(l.a, l.b));
        }
        CHECK(!lines.empty() && Near(lo.y, 4.1f, 1e-3f) && Near(hi.y, 5.9f, 1e-3f) && Near(hi.x, 0.3f, 1e-3f));

        lines.clear();
        ColliderComponent mesh;
        mesh.shape = ColliderShape::Mesh;
        const glm::vec3 mn(-1.0f, 0.0f, -1.0f), mx(1.0f, 0.0f, 1.0f);
        AppendColliderLines(mesh, glm::mat4(1.0f), lines, &mn, &mx);
        CHECK(lines.size() == 12);
    }

    SECTION("Exceção em evento de física não derruba a física nem os outros scripts");
    {
        Scene scene;
        Entity& zona = scene.CreateEntity("ZonaPerigosa");
        zona.Add<ColliderComponent>().isTrigger = true;
        zona.Add<SensorQuebrado>();
        Sensor& sensorBom = zona.Add<Sensor>();

        Entity& bola = AddBox(scene, {0.0f, 0.0f, 0.0f}, "Bola");
        (void)bola;

        SceneRuntime runtime(scene);
        runtime.Start();
        runtime.FixedUpdate(kDt);
        CHECK(runtime.HasError());
        CHECK(runtime.LastError().find("SensorQuebradoDeTeste::OnTriggerEnter") != std::string::npos);
        CHECK(sensorBom.triggerEnters == 1);
    }

    return TestResult();
}
