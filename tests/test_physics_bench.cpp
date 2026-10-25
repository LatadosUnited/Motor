// Desempenho da física: N caixas caem em colunas sobre o chão e empilham; mede o FixedUpdate (só a física roda
// nele: a cena não tem scripts) e o passo do Jolt, com 0 threads extras e com o padrão (núcleos - 1).
// Sem janela. Pelo ctest roda pequeno; para os números do relatório, compile em Release e rode:
//   test_physics_bench.exe <pasta> --caixas 4000 --passos 600
#include "TestUtil.h"

#include <motor/physics/PhysicsSystem.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

using namespace motor;

namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr int kColumnHeight = 10;

struct Result {
    double avgMs = 0.0, maxMs = 0.0;     // FixedUpdate inteiro
    double avgStepMs = 0.0;              // só o PhysicsSystem::Update do Jolt
    double avgSyncMs = 0.0, avgWriteMs = 0.0;  // componentes -> corpos; corpos -> Transforms e eventos
    int maxStepIndex = 0;                // em que passo foi o pior
    double fallingMs = 0.0;              // média do primeiro segundo (tudo caindo e batendo)
    int threads = 0;
    int asleep = 0;                      // corpos dormindo no fim
    int bodies = 0;
    int fellThrough = 0;                 // abaixo do chão (não pode acontecer)
    int toppled = 0;                     // caixas que saíram da coluna
    // Os piores passos depois de 2 s (tudo assentado): ms do passo, do Jolt, da sincronização e da escrita.
    struct Peak { int step; double ms, jolt, sync, write; int active; };
    std::vector<Peak> peaks;
};

Result Run(int boxes, int steps, int threads) {
    PhysicsSettings& settings = PhysicsSettings::Global();
    const int savedThreads = settings.workerThreads;
    settings.workerThreads = threads;

    Scene scene;
    Entity& floor = scene.CreateEntity("Chao");
    floor.transform.position = {0.0f, -0.5f, 0.0f};
    floor.transform.scale = {400.0f, 1.0f, 400.0f};
    floor.Add<ColliderComponent>();

    // Colunas de 10 caixas, com um vão entre elas (caem um pouco e empilham) e um giro pequeno em cada uma.
    const int columns = (boxes + kColumnHeight - 1) / kColumnHeight;
    const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(columns))));
    std::vector<EntityId> ids;
    std::vector<glm::vec2> columnOf;
    for (int i = 0; i < boxes; ++i) {
        const int column = i / kColumnHeight, level = i % kColumnHeight;
        const glm::vec2 xz((column % side - side * 0.5f) * 1.6f, (column / side - side * 0.5f) * 1.6f);
        Entity& box = scene.CreateEntity("Caixa");
        box.transform.position = {xz.x, 0.6f + level * 1.15f, xz.y};
        box.transform.Rotate({0.0f, 1.0f, 0.0f}, 0.15f * static_cast<float>((i * 7) % 11 - 5));
        box.Add<ColliderComponent>();
        box.Add<RigidbodyComponent>();
        ids.push_back(box.id);
        columnOf.push_back(xz);
    }

    SceneRuntime runtime(scene);
    runtime.Start();
    auto* physics = runtime.GetSystem<PhysicsSystem>();

    Result r;
    double total = 0.0, totalStep = 0.0, totalSync = 0.0, totalWrite = 0.0, falling = 0.0;
    const int fallingSteps = std::min(steps, 60);
    for (int i = 0; i < steps; ++i) {
        const auto start = std::chrono::steady_clock::now();
        runtime.FixedUpdate(kDt);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        total += ms;
        totalStep += physics->Stats().stepMs;
        totalSync += physics->Stats().syncMs;
        totalWrite += physics->Stats().writeMs;
        if (ms > r.maxMs) {
            r.maxMs = ms;
            r.maxStepIndex = i;
        }
        if (i < fallingSteps) falling += ms;
        if (i >= 120) {
            const PhysicsStats& st = physics->Stats();
            r.peaks.push_back({i, ms, st.stepMs, st.syncMs, st.writeMs, st.activeBodies});
        }
    }
    std::sort(r.peaks.begin(), r.peaks.end(), [](const Result::Peak& a, const Result::Peak& b) { return a.ms > b.ms; });
    if (r.peaks.size() > 3) r.peaks.resize(3);
    r.avgMs = total / steps;
    r.avgStepMs = totalStep / steps;
    r.avgSyncMs = totalSync / steps;
    r.avgWriteMs = totalWrite / steps;
    r.fallingMs = falling / fallingSteps;
    r.threads = physics->Stats().threads;
    r.bodies = physics->Stats().bodies;
    r.asleep = r.bodies - physics->Stats().activeBodies;
    for (size_t i = 0; i < ids.size(); ++i) {
        const glm::vec3 p = scene.Find(ids[i])->transform.position;
        if (p.y < 0.0f) ++r.fellThrough;
        if (glm::length(glm::vec2(p.x, p.z) - columnOf[i]) > 0.5f) ++r.toppled;
    }
    runtime.Stop();
    settings.workerThreads = savedThreads;
    return r;
}

int Arg(int argc, char** argv, const char* name, int fallback) {
    for (int i = 2; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) return std::max(1, std::atoi(argv[i + 1]));
    }
    return fallback;
}

} // namespace

int main(int argc, char** argv) {
    const int boxes = Arg(argc, argv, "--caixas", 1000);
    const int steps = Arg(argc, argv, "--passos", 240);
    std::printf("%d caixas em colunas de %d, %d passos de 1/60 s, CPU com %u threads\n", boxes, kColumnHeight, steps,
                std::thread::hardware_concurrency());

    SECTION("Caixas caindo e empilhando");
    for (int threads : {0, -1}) {
        const Result r = Run(boxes, steps, threads);
        std::printf("  threads extras %2d: FixedUpdate médio %.2f ms (máx %.2f no passo %d, 1º segundo %.2f);"
                    " Jolt %.2f, sync %.2f, escrita %.2f ms; %d/%d dormindo, %d fora da coluna\n",
                    r.threads, r.avgMs, r.maxMs, r.maxStepIndex, r.fallingMs, r.avgStepMs, r.avgSyncMs, r.avgWriteMs,
                    r.asleep, r.bodies, r.toppled);
        for (const Result::Peak& p : r.peaks) {
            std::printf("    pior depois de 2 s: passo %d, %.2f ms (Jolt %.2f, sync %.2f, escrita %.2f; %d acordados)\n",
                        p.step, p.ms, p.jolt, p.sync, p.write, p.active);
        }
        CHECK(r.fellThrough == 0);
        CHECK(r.bodies == boxes + 1);
    }
    return TestResult();
}
