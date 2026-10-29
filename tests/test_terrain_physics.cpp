// Fase 28: o terreno na física (height field da Jolt por bloco): raios, corpos, personagem e alturas mudadas.

#include "TestUtil.h"

#include <motor/assets/TerrainData.h>
#include <motor/physics/PhysicsSystem.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>

using namespace motor;

namespace {
constexpr float kDt = 1.0f / 60.0f;

bool Near(float a, float b, float eps) { return std::abs(a - b) < eps; }

void Run(SceneRuntime& runtime, float seconds) {
    const int steps = static_cast<int>(std::lround(seconds / kDt));
    for (int i = 0; i < steps; ++i) {
        runtime.FixedUpdate(kDt);
        runtime.Update(kDt);
    }
}

// Morros suaves em 4 x 4 blocos de 32 m (128 m), de -64 a 64 em x e z.
std::shared_ptr<TerrainData> MakeHills() {
    auto data = std::make_shared<TerrainData>();
    for (int z = -2; z < 2; ++z) {
        for (int x = -2; x < 2; ++x) data->AddBlock({x, z});
    }
    for (int gz = -64; gz <= 64; ++gz) {
        for (int gx = -64; gx <= 64; ++gx) {
            const float h = 3.0f * std::sin(static_cast<float>(gx) * 0.08f) * std::cos(static_cast<float>(gz) * 0.06f) +
                            0.02f * static_cast<float>(gx);
            data->SetSample(gx, gz, h);
        }
    }
    return data;
}

Entity& AddTerrain(Scene& scene, std::shared_ptr<TerrainData> data) {
    Entity& e = scene.CreateEntity("Terreno");
    Terrain& terrain = e.Add<Terrain>();
    terrain.terrain.SetData(std::move(data));  // sem arquivo: só na memória
    return e;
}
} // namespace

int main() {
    SECTION("Raio da física acerta o terreno na altura desenhada (HeightAt), com a entidade movida");
    {
        Scene scene;
        auto data = MakeHills();
        Entity& e = AddTerrain(scene, data);
        e.transform.position = {5.0f, 1.0f, -3.0f};
        const EntityId id = e.id;
        SceneRuntime runtime(scene);
        runtime.Start();
        auto* fisica = runtime.GetSystem<PhysicsSystem>();
        Run(runtime, kDt);  // Stats é do último passo
        CHECK(fisica && fisica->Stats().bodies == 16);  // um corpo por bloco, sem Collider
        float worst = 0.0f;
        int hits = 0;
        for (int i = 0; i < 200; ++i) {
            const float x = -58.0f + static_cast<float>((i * 37) % 116) + 0.31f;
            const float z = -58.0f + static_cast<float>((i * 53) % 116) + 0.73f;
            const auto hit = fisica ? fisica->Raycast({x + 5.0f, 40.0f, z - 3.0f}, {0.0f, -1.0f, 0.0f}, 100.0f)
                                    : std::nullopt;
            if (!hit || hit->entity != id) continue;
            ++hits;
            worst = std::max(worst, std::abs(hit->point.y - (data->HeightAt(x, z).value_or(0.0f) + 1.0f)));
            CHECK(hit->normal.y > 0.5f);
        }
        std::printf("  %d raios, maior diferença da altura desenhada: %.4f m\n", hits, worst);
        CHECK(hits == 200 && worst < 0.01f);
        // Fora dos 128 m: nada.
        CHECK(fisica && !fisica->Raycast({100.0f, 40.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f));
        runtime.Stop();
    }

    SECTION("Escala e rotação da entidade valem para a física como para o desenho");
    {
        Scene scene;
        auto data = MakeHills();
        Entity& e = AddTerrain(scene, data);
        e.transform.position = {0.0f, -2.0f, 0.0f};
        e.transform.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        e.transform.scale = {2.0f, 1.5f, 2.0f};
        const glm::mat4 world = scene.WorldMatrix(e.id);
        SceneRuntime runtime(scene);
        runtime.Start();
        auto* fisica = runtime.GetSystem<PhysicsSystem>();
        float worst = 0.0f;
        for (int i = 0; i < 50; ++i) {
            const float x = -50.0f + static_cast<float>(i) * 2.0f + 0.4f, z = 20.0f - static_cast<float>(i) * 0.7f;
            const glm::vec3 local = glm::vec3(world[0]) * x + glm::vec3(world[2]) * z + glm::vec3(world[3]);
            const glm::vec3 expected = glm::vec3(world * glm::vec4(x, data->HeightAt(x, z).value_or(0.0f), z, 1.0f));
            const auto hit = fisica ? fisica->Raycast({local.x, 60.0f, local.z}, {0.0f, -1.0f, 0.0f}, 200.0f) : std::nullopt;
            CHECK(hit.has_value());
            if (hit) worst = std::max(worst, std::abs(hit->point.y - expected.y));
        }
        std::printf("  girado e escalado: maior diferença %.4f m\n", worst);
        CHECK(worst < 0.02f);
        runtime.Stop();
    }

    SECTION("Corpo cai e para no terreno; personagem fica no chão e anda pelo morro");
    {
        Scene scene;
        auto data = MakeHills();
        const EntityId terrainId = AddTerrain(scene, data).id;
        // Esfera num vale plano o bastante para não rolar longe: perto de x = -20 (sen(-1,6) ~ -1).
        Entity& ball = scene.CreateEntity("Bola");
        ball.transform.position = {-19.6f, 10.0f, 0.0f};
        ColliderComponent& collider = ball.Add<ColliderComponent>();
        collider.shape = ColliderShape::Sphere;
        collider.radius = 0.5f;
        ball.Add<RigidbodyComponent>();
        const EntityId ballId = ball.id;
        Entity& player = scene.CreateEntity("Jogador");
        player.transform.position = {10.0f, 8.0f, 10.0f};
        player.Add<CharacterControllerComponent>();
        const EntityId playerId = player.id;

        SceneRuntime runtime(scene);
        runtime.Start();
        auto* fisica = runtime.GetSystem<PhysicsSystem>();
        Run(runtime, 4.0f);
        const glm::vec3 b = scene.Find(ballId)->transform.position;
        const float ground = data->HeightAt(b.x, b.z).value_or(-100.0f);
        std::printf("  bola em (%.2f, %.2f, %.2f), chão a %.2f\n", b.x, b.y, b.z, ground);
        CHECK(Near(b.y - ground, 0.5f, 0.1f));
        CHECK(fisica && fisica->IsGrounded(playerId));
        glm::vec3 p = scene.Find(playerId)->transform.position;
        CHECK(Near(p.y - data->HeightAt(p.x, p.z).value_or(-100.0f), 0.9f, 0.15f));

        // Andando 3 m/s em x por 4 s: sobe e desce o morro sem atravessar nem voar.
        for (int i = 0; i < 240 && fisica; ++i) {
            fisica->MoveCharacter(playerId, {3.0f, 0.0f, 0.0f});
            runtime.FixedUpdate(kDt);
            runtime.Update(kDt);
            p = scene.Find(playerId)->transform.position;
            const float above = p.y - data->HeightAt(p.x, p.z).value_or(-100.0f);
            CHECK(above > 0.5f && above < 1.4f);
        }
        std::printf("  personagem andou até x = %.2f\n", p.x);
        CHECK(p.x > 18.0f);
        const auto hit = fisica ? fisica->Raycast({0.0f, 30.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f) : std::nullopt;
        CHECK(hit && hit->entity == terrainId);
        runtime.Stop();
    }

    SECTION("Alturas mudadas refazem os corpos; sem Colisão ou desativado, nada");
    {
        Scene scene;
        auto data = MakeHills();
        Entity& e = AddTerrain(scene, data);
        const EntityId id = e.id;
        // Só a colisão do terreno já é física (o mundo é criado sem nenhum Collider).
        SceneRuntime runtime(scene);
        runtime.Start();
        auto* fisica = runtime.GetSystem<PhysicsSystem>();
        auto groundAt = [&](float x, float z) {
            const auto hit = fisica ? fisica->Raycast({x, 50.0f, z}, {0.0f, -1.0f, 0.0f}, 100.0f) : std::nullopt;
            return hit ? hit->point.y : -1000.0f;
        };
        CHECK(Near(groundAt(4.0f, 4.0f), data->HeightAt(4.0f, 4.0f).value_or(0.0f), 0.01f));
        data->SetSample(4, 4, 12.0f);
        Run(runtime, kDt);
        CHECK(Near(groundAt(4.0f, 4.0f), 12.0f, 0.01f));
        // Só os blocos da amostra e os que a têm na normal da borda (aqui, os 4 em volta do canto (0, 0)), não os 16.
        CHECK(fisica && fisica->Stats().terrainBlocksBuilt > 0 && fisica->Stats().terrainBlocksBuilt <= 4);
        CHECK(fisica && fisica->Stats().bodies == 16);
        // Pintar não mexe na colisão: nenhum bloco refeito.
        data->SetWeights(40, 40, {0.0f, 1.0f, 0.0f, 0.0f});
        Run(runtime, kDt);
        CHECK(fisica && fisica->Stats().terrainBlocksBuilt == 0);
        // Bloco tirado sai da física; posto de volta, entra sozinho.
        CHECK(data->RemoveBlock({1, 1}));
        Run(runtime, kDt);
        CHECK(fisica && fisica->Stats().bodies == 15 && groundAt(40.0f, 40.0f) < -999.0f);
        CHECK(data->AddBlock({1, 1}, 2.0f));
        Run(runtime, kDt);
        CHECK(fisica && fisica->Stats().bodies == 16 && fisica->Stats().terrainBlocksBuilt <= 4);

        scene.Find(id)->Get<Terrain>()->collision = false;
        Run(runtime, kDt);
        CHECK(fisica && fisica->Stats().bodies == 0 && groundAt(4.0f, 4.0f) < -999.0f);
        scene.Find(id)->Get<Terrain>()->collision = true;
        Run(runtime, kDt);
        CHECK(fisica && fisica->Stats().bodies == 16);
        scene.Find(id)->active = false;
        Run(runtime, kDt);
        CHECK(fisica && fisica->Stats().bodies == 0);
        runtime.Stop();

        Scene off;
        AddTerrain(off, MakeHills()).Get<Terrain>()->collision = false;
        SceneRuntime idle(off);
        idle.Start();
        Run(idle, kDt);
        CHECK(idle.GetSystem<PhysicsSystem>() && idle.GetSystem<PhysicsSystem>()->Stats().bodies == 0);
        idle.Stop();
    }

    return TestResult();
}
