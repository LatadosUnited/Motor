// Percepção (Fase 37): os componentes Perception e PerceptionTarget (campos, padrões, grupos e a ida e volta pela
// cena). Não usa OpenGL nem janela.
#include "TestUtil.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneSerializer.h>

#include <string>

using namespace motor;

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

    return TestResult();
}
