// Percepção (Fase 37): os componentes Perception e PerceptionTarget (campos, padrões, grupos e a ida e volta pela
// cena) e o PerceptionSystem no SceneRuntime (visão por alcance, cone e linha de visada). Não usa OpenGL nem janela.
#include "TestUtil.h"

#include <motor/ai/PerceptionSystem.h>
#include <motor/assets/AssetLibrary.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/scene/SceneSerializer.h>

#include <glm/geometric.hpp>

#include <string>
#include <vector>

using namespace motor;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// Chão 20 x 20 (topo em y = 0) e uma parede de x = -6 a 6 em z = 0, 3 m de altura.
void AddRoom(Scene& scene) {
    Entity& floor = scene.CreateEntity("Chão");
    floor.transform.position = {0.0f, -0.5f, 0.0f};
    floor.Add<ColliderComponent>().size = {20.0f, 1.0f, 20.0f};
    Entity& wall = scene.CreateEntity("Parede");
    wall.transform.position = {0.0f, 1.5f, 0.0f};
    wall.Add<ColliderComponent>().size = {12.0f, 3.0f, 1.0f};
}

EntityId AddTarget(Scene& scene, const char* name, const glm::vec3& position) {
    Entity& e = scene.CreateEntity(name);
    e.transform.position = position;
    e.Add<PerceptionTarget>();
    return e.id;
}

} // namespace

int main() {
    SECTION("Componentes registrados, com os nomes do Inspector");
    {
        const ComponentRegistry& registry = ComponentRegistry::Instance();
        const ComponentType* perception = registry.Find("Perception");
        const ComponentType* target = registry.Find("PerceptionTarget");
        CHECK(perception && perception->label == "Percepção");
        CHECK(target && target->label == "Alvo de percepção");
        CHECK(perception && perception->fields.size() == 7);
        CHECK(target && target->fields.size() == 4);
    }

    SECTION("Grupos que o agente percebe");
    {
        Perception p;
        CHECK(p.Detects("Jogador") && p.Detects("Ovelha"));  // vazio: todos
        p.detects = " Jogador , Lobo";
        CHECK(p.Detects("Jogador") && p.Detects("Lobo"));
        CHECK(!p.Detects("Ovelha") && !p.Detects("Jogado") && !p.Detects(""));
        p.detects = " , ";
        CHECK(p.Detects("Ovelha"));  // só vírgulas e espaços: todos
    }

    SECTION("Ida e volta pela cena");
    {
        Scene scene;
        Entity& guard = scene.CreateEntity("Guarda");
        Perception& eyes = guard.Add<Perception>();
        eyes.sightRange = 20.0f;
        eyes.sightAngle = 90.0f;
        eyes.nearRange = 2.0f;
        eyes.eyeHeight = 1.7f;
        eyes.hearing = 0.5f;
        eyes.memory = 8.0f;
        eyes.detects = "Jogador";
        Entity& player = scene.CreateEntity("Jogador");
        PerceptionTarget& seen = player.Add<PerceptionTarget>();
        seen.group = "Herói";
        seen.height = 0.9f;
        seen.visibility = 0.25f;
        seen.soundsAreNoise = false;

        AssetLibrary assets;
        const std::string text = SceneSerializer::ToJson(scene, assets);
        Scene loaded;
        std::string error;
        CHECK(SceneSerializer::FromJson(text, loaded, assets, &error));
        CHECK(loaded == scene);
        CHECK(SceneSerializer::ToJson(loaded, assets) == text);

        const char* old = R"({"format": "motor-scene", "version": 6, "entities": [
            {"name": "Veado", "components": [{"type": "Perception"}, {"type": "PerceptionTarget"}]}]})";
        Scene defaults;
        CHECK(SceneSerializer::FromJson(old, defaults, assets, &error));
        const Entity* deer = defaults.Entities().empty() ? nullptr : defaults.Entities().front().get();
        CHECK(deer && deer->Get<Perception>() && *deer->Get<Perception>() == Perception{});
        CHECK(deer && deer->Get<PerceptionTarget>() && *deer->Get<PerceptionTarget>() == PerceptionTarget{});
    }

    SECTION("Visão: alcance, cone e linha de visada");
    {
        // O guarda em z = 5 olha para -Z (a parede em z = 0). Alvos: na frente, atrás da parede, atrás dele (longe e
        // perto), de lado (45°) e um com Collider próprio.
        Scene scene;
        AddRoom(scene);
        Entity& guardEntity = scene.CreateEntity("Guarda");
        guardEntity.transform.position = {0.0f, 0.0f, 5.0f};
        guardEntity.Add<Perception>();
        const EntityId guard = guardEntity.id;
        Entity& shield = scene.CreateEntity("Escudo", guard);  // Collider filho do guarda, na frente dos olhos
        shield.transform.position = {0.0f, 1.5f, -0.6f};
        shield.Add<ColliderComponent>().size = {1.0f, 1.0f, 0.1f};
        const EntityId front = AddTarget(scene, "Na frente", {0.0f, 0.0f, 2.0f});
        const EntityId walled = AddTarget(scene, "Atrás da parede", {0.0f, 0.0f, -3.0f});
        const EntityId behind = AddTarget(scene, "Atrás do guarda", {0.0f, 0.0f, 8.0f});
        const EntityId close = AddTarget(scene, "Colado", {0.5f, 0.0f, 6.0f});
        const EntityId side = AddTarget(scene, "De lado", {2.0f, 0.0f, 3.0f});
        const EntityId boxed = AddTarget(scene, "Com corpo", {-2.0f, 0.0f, 3.5f});
        scene.Find(boxed)->Add<ColliderComponent>().size = {1.0f, 2.0f, 1.0f};
        scene.Find(boxed)->Get<ColliderComponent>()->center = {0.0f, 1.0f, 0.0f};

        SceneRuntime runtime(scene);
        runtime.Start();
        PerceptionSystem* senses = runtime.GetSystem<PerceptionSystem>();
        CHECK(senses != nullptr);
        if (senses) {
            runtime.FixedUpdate(kDt);
            CHECK(senses->CanSee(guard, front));
            CHECK(!senses->CanSee(guard, walled));  // a parede tapa
            CHECK(!senses->CanSee(guard, behind));  // fora do cone
            CHECK(senses->CanSee(guard, close));    // perto: qualquer direção
            CHECK(senses->CanSee(guard, side));     // 45°, dentro dos 60° de cada lado
            CHECK(senses->CanSee(guard, boxed));    // o Collider do próprio alvo não tapa
            const std::vector<EntityId> order{close, boxed, side, front};  // o mais perto primeiro
            CHECK(senses->GetSeen(guard) == order);
            for (EntityId t : {front, walled, behind, close, side, boxed})
                CHECK(senses->CheckSight(guard, t) == senses->CanSee(guard, t));
            CHECK(senses->Stats().perceivers == 1 && senses->Stats().targets == 6 && senses->Stats().checks == 6);
            CHECK(senses->Stats().raycasts == 5);  // o de trás, fora do cone, não chega ao raio

            Perception& eyes = *scene.Find(guard)->Get<Perception>();
            eyes.sightAngle = 60.0f;  // 30° de cada lado: o de lado sai
            runtime.FixedUpdate(kDt);
            CHECK(!senses->CanSee(guard, side) && senses->CanSee(guard, front));

            eyes.sightRange = 2.5f;  // o da frente está a uns 3 m dos olhos
            runtime.FixedUpdate(kDt);
            CHECK(!senses->CanSee(guard, front) && senses->CanSee(guard, close));
            eyes.sightRange = 15.0f;
            scene.Find(front)->Get<PerceptionTarget>()->visibility = 0.1f;  // agachado: 1,5 m de alcance
            runtime.FixedUpdate(kDt);
            CHECK(!senses->CanSee(guard, front));
            scene.Find(front)->Get<PerceptionTarget>()->visibility = 1.0f;

            eyes.detects = "Lobo";  // não percebe o grupo dos alvos
            runtime.FixedUpdate(kDt);
            CHECK(senses->GetSeen(guard).empty());
            eyes.detects.clear();

            scene.Find(front)->active = false;  // desligado: ninguém vê
            runtime.FixedUpdate(kDt);
            CHECK(!senses->CanSee(guard, front) && !senses->CheckSight(guard, front));
            scene.Find(front)->active = true;

            // Virado para +Z: vê o de trás e não vê mais o da frente.
            eyes.sightAngle = 120.0f;
            scene.Find(guard)->transform.SetEulerDegrees({0.0f, 180.0f, 0.0f});
            runtime.FixedUpdate(kDt);
            CHECK(senses->CanSee(guard, behind) && !senses->CanSee(guard, front));

            // Sem Percepção, nada visto.
            scene.Find(guard)->Remove<Perception>();
            runtime.FixedUpdate(kDt);
            CHECK(senses->GetSeen(guard).empty() && senses->Stats().perceivers == 0);
        }
        runtime.Stop();
    }

    SECTION("Visão: olhos e ponto olhado no mundo");
    {
        Scene scene;
        Entity& parent = scene.CreateEntity("Torre");
        parent.transform.position = {0.0f, 10.0f, 0.0f};
        Entity& guard = scene.CreateEntity("Vigia", parent.id);
        guard.transform.position = {1.0f, 0.0f, 0.0f};
        guard.transform.SetEulerDegrees({0.0f, 90.0f, 0.0f});  // -Z girado 90° em Y: olha para -X
        guard.Add<Perception>().eyeHeight = 2.0f;
        const PerceptionEye eye = PerceptionEyeOf(scene, guard.id);
        CHECK(glm::length(eye.position - glm::vec3(1.0f, 12.0f, 0.0f)) < 1e-4f);
        CHECK(glm::length(eye.forward - glm::vec3(-1.0f, 0.0f, 0.0f)) < 1e-4f);
        const EntityId t = AddTarget(scene, "Alvo", {3.0f, 0.0f, 0.0f});
        scene.Find(t)->Get<PerceptionTarget>()->height = 0.8f;
        CHECK(glm::length(PerceptionPointOf(scene, t) - glm::vec3(3.0f, 0.8f, 0.0f)) < 1e-4f);
    }

    return TestResult();
}
