// Percepção (Fase 37): os componentes Perception e PerceptionTarget (campos, padrões, grupos e a ida e volta pela
// cena) e o PerceptionSystem no SceneRuntime: visão por alcance, cone e linha de visada (Tarefa 2) e audição, com os
// ruídos dos scripts e os sons das Fontes de áudio dos alvos (Tarefa 3), a memória (Tarefa 4) e a grade de alvos com
// o orçamento de raios por passo (Tarefa 5). Não usa OpenGL, janela nem placa de som.
#include "TestUtil.h"

#include <motor/ai/PerceptionSystem.h>
#include <motor/assets/AssetLibrary.h>
#include <motor/audio/AudioSystem.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/scene/SceneSerializer.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

// Um .wav mono de 16 bits com um seno de 440 Hz.
void WriteSineWav(const std::filesystem::path& file, float seconds, int rate = 48000) {
    const uint32_t frames = static_cast<uint32_t>(seconds * static_cast<float>(rate));
    std::vector<int16_t> samples(frames);
    for (uint32_t i = 0; i < frames; ++i)
        samples[i] = static_cast<int16_t>(16000.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * i / rate));
    std::ofstream out(file, std::ios::binary);
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4);
    u32(36 + frames * 2);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);  // PCM
    u16(1);  // mono
    u32(static_cast<uint32_t>(rate));
    u32(static_cast<uint32_t>(rate) * 2);
    u16(2);
    u16(16);
    out.write("data", 4);
    u32(frames * 2);
    out.write(reinterpret_cast<const char*>(samples.data()), frames * 2);
}

EntityId AddTarget(Scene& scene, const char* name, const glm::vec3& position) {
    Entity& e = scene.CreateEntity(name);
    e.transform.position = position;
    e.Add<PerceptionTarget>();
    return e.id;
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path dir = TestDir(argc, argv, "test_perception_tmp");
    AudioSettings::Global().useDevice = false;  // nunca abre a placa de som nos testes

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
        seen.soundRange = 4.0f;

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

    SECTION("Audição: ruídos dos scripts");
    {
        // Dois ouvintes, um surdo pela metade, e a parede entre eles e o jogador (o som atravessa).
        Scene scene;
        AddRoom(scene);
        Entity& guardEntity = scene.CreateEntity("Guarda");
        guardEntity.transform.position = {0.0f, 0.0f, 5.0f};
        guardEntity.Add<Perception>();
        const EntityId guard = guardEntity.id;
        Entity& deafEntity = scene.CreateEntity("Meio surdo");
        deafEntity.transform.position = {4.0f, 0.0f, 5.0f};
        deafEntity.Add<Perception>().hearing = 0.5f;
        const EntityId deaf = deafEntity.id;
        Entity& foot = scene.CreateEntity("Pé", guard);  // filho do guarda: os passos dele
        const EntityId player = AddTarget(scene, "Jogador", {0.0f, 0.0f, -3.0f});
        const EntityId sheep = AddTarget(scene, "Ovelha", {4.0f, 0.0f, -3.0f});
        scene.Find(sheep)->Get<PerceptionTarget>()->group = "Ovelha";
        scene.Find(guard)->Get<Perception>()->detects = "Jogador";

        SceneRuntime runtime(scene);
        runtime.Start();
        PerceptionSystem* senses = runtime.GetSystem<PerceptionSystem>();
        CHECK(senses != nullptr);
        if (senses) {
            // Passo do jogador atrás da parede: 8 m dos olhos do guarda e 9 m dos do outro.
            const glm::vec3 step = PerceptionPointOf(scene, player);
            senses->MakeNoise(step, 10.0f, player);
            CHECK(senses->GetHeard(guard).empty());  // só no próximo passo
            runtime.FixedUpdate(kDt);
            CHECK(senses->Stats().noises == 1);
            CHECK(senses->CanHear(guard, player));
            CHECK(!senses->CanSee(guard, player));   // ouve, mas a parede tapa a visão
            CHECK(!senses->CanHear(deaf, player));   // 9 m > 10 x 0,5
            const std::vector<HeardNoise> heard = senses->GetHeard(guard);
            CHECK(heard.size() == 1 && heard[0].source == player);
            CHECK(heard.size() == 1 && glm::length(heard[0].position - step) < 1e-4f);
            CHECK(heard.size() == 1 && std::abs(heard[0].distance - glm::distance(PerceptionEyeOf(scene, guard).position,
                                                                                  step)) < 1e-3f);

            runtime.FixedUpdate(kDt);  // um ruído dura um passo
            CHECK(senses->GetHeard(guard).empty() && senses->Stats().noises == 0);

            // Dois do mesmo jogador: o guarda fica com o mais perto, que chega também ao meio surdo.
            senses->MakeNoise(step, 9.0f, player);
            senses->MakeNoise({2.0f, 1.0f, 3.0f}, 6.0f, player);
            runtime.FixedUpdate(kDt);
            CHECK(senses->GetHeard(guard).size() == 1);  // de cada fonte, só o mais perto
            CHECK(senses->GetHeard(guard).size() == 1 && senses->GetHeard(guard)[0].position == glm::vec3(2.0f, 1.0f, 3.0f));
            CHECK(senses->CanHear(deaf, player));  // 2,9 m < 6 x 0,5

            // Grupo que o guarda não percebe; os próprios passos; um ruído sem dono (todos ouvem).
            senses->MakeNoise(PerceptionPointOf(scene, sheep), 20.0f, sheep);
            senses->MakeNoise({0.0f, 0.0f, 5.0f}, 20.0f, foot.id);
            senses->MakeNoise({0.0f, 1.0f, 0.0f}, 20.0f);
            senses->MakeNoise({0.0f, 1.0f, 0.0f}, 0.0f, player);  // alcance 0: nada
            runtime.FixedUpdate(kDt);
            CHECK(senses->Stats().noises == 3);
            const std::vector<HeardNoise> mixed = senses->GetHeard(guard);
            CHECK(mixed.size() == 1 && mixed[0].source == kNullEntity);
            CHECK(!senses->CanHear(guard, sheep) && !senses->CanHear(guard, foot.id));
            const std::vector<HeardNoise> other = senses->GetHeard(deaf);  // percebe todos os grupos
            CHECK(other.size() == 3);
            CHECK(other.size() == 3 && other[0].distance <= other[1].distance && other[1].distance <= other[2].distance);

            scene.Find(guard)->Get<Perception>()->hearing = 0.0f;  // surdo
            senses->MakeNoise({0.0f, 1.0f, 4.0f}, 20.0f, player);
            runtime.FixedUpdate(kDt);
            CHECK(senses->GetHeard(guard).empty() && senses->CanHear(deaf, player));
        }
        runtime.Stop();
    }

    SECTION("Audição: os sons da Fonte de áudio de um alvo");
    {
        const std::filesystem::path wav = dir / "Passos.wav";
        WriteSineWav(wav, 0.5f);
        Scene scene;
        Entity& guardEntity = scene.CreateEntity("Guarda");
        guardEntity.Add<Perception>();
        const EntityId guard = guardEntity.id;
        const EntityId player = AddTarget(scene, "Jogador", {0.0f, 0.0f, 6.0f});
        scene.Find(player)->Get<PerceptionTarget>()->soundRange = 8.0f;
        AudioSource& steps = scene.Find(player)->Add<AudioSource>();
        steps.clip.file = wav;
        steps.playOnStart = false;
        steps.loop = true;
        const EntityId far = AddTarget(scene, "Longe", {0.0f, 0.0f, 12.0f});
        AudioSource& farSteps = scene.Find(far)->Add<AudioSource>();
        farSteps.clip.file = wav;
        farSteps.playOnStart = false;
        farSteps.loop = true;

        SceneRuntime runtime(scene);
        runtime.Start();
        PerceptionSystem* senses = runtime.GetSystem<PerceptionSystem>();
        AudioSystem* audio = runtime.GetSystem<AudioSystem>();
        CHECK(senses && audio);
        if (senses && audio) {
            runtime.FixedUpdate(kDt);
            CHECK(!senses->CanHear(guard, player));  // parado: sem som
            CHECK(audio->Play(player) && audio->Play(far));
            runtime.FixedUpdate(kDt);
            CHECK(senses->CanHear(guard, player));   // 6,2 m < 8
            CHECK(!senses->CanHear(guard, far));     // 12 m > 10 (o alcance padrão)
            CHECK(senses->Stats().noises == 2);
            CHECK(senses->GetHeard(guard).size() == 1 &&
                  glm::length(senses->GetHeard(guard)[0].position - glm::vec3(0.0f, 0.0f, 6.0f)) < 1e-4f);

            AudioSource& playing = *scene.Find(player)->Get<AudioSource>();
            playing.volume = 0.5f;  // metade do volume: 4 m
            runtime.FixedUpdate(kDt);
            CHECK(!senses->CanHear(guard, player));
            playing.volume = 1.0f;
            scene.Find(player)->Get<PerceptionTarget>()->soundRange = 0.0f;  // os sons dele não chegam
            runtime.FixedUpdate(kDt);
            CHECK(!senses->CanHear(guard, player));
            scene.Find(player)->Get<PerceptionTarget>()->soundRange = 8.0f;
            CHECK(audio->Stop(player));
            runtime.FixedUpdate(kDt);
            CHECK(!senses->CanHear(guard, player));
        }
        runtime.Stop();
    }

    SECTION("Memória: última posição, há quanto tempo e esquecer");
    {
        Scene scene;
        AddRoom(scene);
        Entity& guardEntity = scene.CreateEntity("Guarda");
        guardEntity.transform.position = {0.0f, 0.0f, 5.0f};
        guardEntity.Add<Perception>().memory = 1.0f;
        const EntityId guard = guardEntity.id;
        const EntityId player = AddTarget(scene, "Jogador", {1.0f, 0.0f, 2.0f});
        const EntityId sheep = AddTarget(scene, "Ovelha", {-8.0f, 0.0f, -3.0f});  // atrás da parede

        SceneRuntime runtime(scene);
        runtime.Start();
        PerceptionSystem* senses = runtime.GetSystem<PerceptionSystem>();
        CHECK(senses != nullptr);
        if (senses) {
            CHECK(!senses->Recall(guard, player).has_value() && senses->GetMemory(guard).empty());
            runtime.FixedUpdate(kDt);
            auto seen = senses->Recall(guard, player);
            CHECK(seen && seen->seen && !seen->heard && seen->bySight && seen->age == 0.0f);
            CHECK(seen && seen->position == glm::vec3(1.0f, 0.0f, 2.0f));  // a origem do alvo
            CHECK(!senses->Recall(guard, sheep).has_value());

            // O jogador some atrás da parede: o guarda lembra onde o viu da última vez.
            scene.Find(player)->transform.position = {1.0f, 0.0f, 2.5f};
            runtime.FixedUpdate(kDt);
            scene.Find(player)->transform.position = {1.0f, 0.0f, -4.0f};
            for (int i = 0; i < 30; ++i) runtime.FixedUpdate(kDt);
            seen = senses->Recall(guard, player);
            CHECK(seen && !seen->seen && seen->bySight);
            CHECK(seen && seen->position == glm::vec3(1.0f, 0.0f, 2.5f));
            CHECK(seen && std::abs(seen->age - 30 * kDt) < 1e-4f);

            // A ovelha faz barulho: lembrada pelo ruído; o ruído sem dono não fica.
            senses->MakeNoise({-8.0f, 0.5f, -3.0f}, 20.0f, sheep);
            senses->MakeNoise({5.0f, 0.5f, 5.0f}, 20.0f);
            runtime.FixedUpdate(kDt);
            const auto heard = senses->Recall(guard, sheep);
            CHECK(heard && heard->heard && !heard->seen && !heard->bySight && heard->age == 0.0f);
            CHECK(heard && heard->position == glm::vec3(-8.0f, 0.5f, -3.0f));
            std::vector<PerceptionMemory> memory = senses->GetMemory(guard);
            CHECK(memory.size() == 2 && memory[0].target == sheep && memory[1].target == player);  // a mais recente antes

            // Passou da memória (1 s) sem perceber: esquece o jogador, mas ainda lembra a ovelha.
            for (int i = 0; i < 31; ++i) runtime.FixedUpdate(kDt);
            CHECK(!senses->Recall(guard, player).has_value());
            CHECK(senses->Recall(guard, sheep).has_value());

            // Visto e ouvido no mesmo passo: a posição é a de quem viu.
            scene.Find(player)->transform.position = {0.0f, 0.0f, 3.0f};
            senses->MakeNoise({0.0f, 0.0f, 2.0f}, 20.0f, player);
            runtime.FixedUpdate(kDt);
            seen = senses->Recall(guard, player);
            CHECK(seen && seen->seen && seen->heard && seen->bySight && seen->position == glm::vec3(0.0f, 0.0f, 3.0f));

            senses->Forget(guard, player);
            CHECK(!senses->Recall(guard, player).has_value() && senses->Recall(guard, sheep).has_value());
            runtime.FixedUpdate(kDt);  // ainda vê: volta a lembrar
            CHECK(senses->Recall(guard, player).has_value());
            senses->Forget(guard);
            CHECK(senses->GetMemory(guard).empty());

            // Quem sai da cena é esquecido; sem memória (0), só lembra enquanto percebe.
            runtime.FixedUpdate(kDt);
            scene.DestroyEntity(player);
            runtime.FixedUpdate(kDt);
            CHECK(senses->GetMemory(guard).empty());
            senses->MakeNoise({-8.0f, 0.5f, -3.0f}, 20.0f, sheep);
            scene.Find(guard)->Get<Perception>()->memory = 0.0f;
            runtime.FixedUpdate(kDt);
            CHECK(senses->Recall(guard, sheep).has_value());
            runtime.FixedUpdate(kDt);
            CHECK(!senses->Recall(guard, sheep).has_value());
        }
        runtime.Stop();
    }

    SECTION("Orçamento de raios: repartidos entre os passos, os mais antigos primeiro");
    {
        // 10 agentes em fila olhando para 10 alvos na frente deles: 100 pares no cone, orçamento de 30 raios.
        Scene scene;
        AddRoom(scene);
        std::vector<EntityId> agents, targets;
        for (int i = 0; i < 10; ++i) {
            Entity& g = scene.CreateEntity("Guarda");
            g.transform.position = {-4.5f + i, 0.0f, 9.0f};
            g.Add<Perception>().sightAngle = 360.0f;
            agents.push_back(g.id);
            targets.push_back(AddTarget(scene, "Alvo", {-4.5f + i, 0.0f, 4.0f}));
        }
        const EntityId far = AddTarget(scene, "Longe", {60.0f, 0.0f, 60.0f});  // fora da grade de todos

        SceneRuntime runtime(scene);
        runtime.Start();
        PerceptionSystem* senses = runtime.GetSystem<PerceptionSystem>();
        CHECK(senses && senses->RaycastBudget() == 128);
        if (senses) {
            senses->SetRaycastBudget(30);
            const auto seenPairs = [&] {
                int n = 0;
                for (EntityId a : agents) n += static_cast<int>(senses->GetSeen(a).size());
                return n;
            };
            runtime.FixedUpdate(kDt);
            CHECK(senses->Stats().raycasts == 30 && senses->Stats().deferred == 70);
            CHECK(seenPairs() == 30);  // os pares sem raio ainda não veem
            for (int i = 0; i < 3; ++i) runtime.FixedUpdate(kDt);
            CHECK(seenPairs() == 100);  // em 4 passos, todos tiveram o raio
            CHECK(!senses->CanSee(agents[0], far));

            // O primeiro alvo vai para trás da parede: em até 4 passos ninguém o vê mais.
            scene.Find(targets[0])->transform.position = {-4.5f, 0.0f, -2.0f};
            int steps = 0;
            const auto anyoneSees = [&](EntityId t) {
                return std::any_of(agents.begin(), agents.end(), [&](EntityId a) { return senses->CanSee(a, t); });
            };
            do {
                runtime.FixedUpdate(kDt);
                ++steps;
            } while (anyoneSees(targets[0]) && steps < 20);
            CHECK(!anyoneSees(targets[0]) && steps <= 4);
            std::printf("  atrás da parede: ninguém vê mais em %d passo(s)\n", steps);

            // Fora do cone sai na hora (o teste barato roda todo passo); com orçamento de sobra, tudo a cada passo.
            for (EntityId a : agents) scene.Find(a)->Get<Perception>()->sightAngle = 20.0f;
            runtime.FixedUpdate(kDt);
            // Só o da frente de cada um no cone (e o primeiro alvo, longe atrás da parede, também no do vizinho): 11
            // raios, e só os 9 sem parede vistos.
            CHECK(senses->Stats().raycasts == 11 && seenPairs() == 9);
            senses->SetRaycastBudget(0);
            CHECK(senses->RaycastBudget() == 1);
        }
        runtime.Stop();
    }

    SECTION("Muitos agentes: 100 olhando 20 alvos que andam");
    {
        // Chão 100 x 100 com 36 pilares, 100 guardas numa grade (cada um virado para um lado) e 20 alvos andando em
        // círculos entre eles. O critério da fase: menos de 1 ms por passo em Release (em Debug só mostra o número).
        Scene scene;
        Entity& floor = scene.CreateEntity("Chão");
        floor.transform.position = {0.0f, -0.5f, 0.0f};
        floor.Add<ColliderComponent>().size = {100.0f, 1.0f, 100.0f};
        for (int x = 0; x < 6; ++x)
            for (int z = 0; z < 6; ++z) {
                Entity& pillar = scene.CreateEntity("Pilar");
                pillar.transform.position = {-35.0f + 14.0f * x, 1.5f, -35.0f + 14.0f * z};
                pillar.Add<ColliderComponent>().size = {2.0f, 3.0f, 2.0f};
            }
        std::vector<EntityId> guards;
        for (int i = 0; i < 100; ++i) {
            Entity& g = scene.CreateEntity("Guarda");
            g.transform.position = {-45.0f + 10.0f * (i % 10), 0.0f, -45.0f + 10.0f * (i / 10)};
            g.transform.SetEulerDegrees({0.0f, 37.0f * i, 0.0f});
            g.Add<Perception>();
            guards.push_back(g.id);
        }
        std::vector<EntityId> walkers;
        for (int i = 0; i < 20; ++i) walkers.push_back(AddTarget(scene, "Andando", {0.0f, 0.0f, 0.0f}));
        const auto place = [&](int step) {
            for (int i = 0; i < 20; ++i) {
                const float a = 0.3f * i + step * kDt * 0.5f;
                const float r = 5.0f + 2.0f * i;
                scene.Find(walkers[i])->transform.position = {r * std::cos(a), 0.0f, r * std::sin(a)};
            }
        };

        SceneRuntime runtime(scene);
        runtime.Start();
        PerceptionSystem* senses = runtime.GetSystem<PerceptionSystem>();
        CHECK(senses != nullptr);
        if (senses) {
            place(0);
            runtime.FixedUpdate(kDt);  // a física cria os corpos no primeiro passo
            double total = 0.0, worst = 0.0;
            int rays = 0, seen = 0;
            constexpr int kSteps = 240;
            for (int step = 1; step <= kSteps; ++step) {
                place(step);
                runtime.FixedUpdate(kDt);
                total += senses->Stats().ms;
                worst = std::max(worst, senses->Stats().ms);
                rays += senses->Stats().raycasts;
                for (EntityId g : guards) seen += static_cast<int>(senses->GetSeen(g).size());
            }
            const double mean = total / kSteps;
            std::printf("  %d agentes, %d alvos: %.3f ms por passo (pior %.3f), %d pares olhados, %.1f raios e %.1f "
                        "vistos por passo\n",
                        senses->Stats().perceivers, senses->Stats().targets, mean, worst, senses->Stats().checks,
                        static_cast<double>(rays) / kSteps, static_cast<double>(seen) / kSteps);
            CHECK(senses->Stats().perceivers == 100 && senses->Stats().targets == 20);
            CHECK(senses->Stats().checks < 100 * 20 / 2);  // a grade: cada um só olha os alvos das células perto
            CHECK(seen > 0);
#ifdef NDEBUG
            CHECK(mean < 1.0);
#endif
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
