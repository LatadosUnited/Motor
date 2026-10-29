// Fase 28: raycast do editor pela grade. Com os objetos estáticos (guardados no RenderWorld) o resultado tem de ser o
// mesmo que testando todos um a um.

#include "TestUtil.h"

#include "motor/scene/Components.h"
#include "motor/scene/Scene.h"

#include <cmath>
#include <random>

using namespace motor;

namespace {
// Cubos e esferas espalhados em 400 x 400 m, alguns empilhados (filhos) e alguns desativados.
void Fill(Scene& scene, bool isStatic) {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> pos(-200.0f, 200.0f), size(0.5f, 6.0f), angle(0.0f, 3.0f);
    const auto cube = Mesh::CreateCube();
    const auto sphere = Mesh::CreateSphere();
    EntityId last = kNullEntity;
    for (int i = 0; i < 1500; ++i) {
        const bool child = i % 11 == 0 && last != kNullEntity;
        Entity& e = scene.CreateEntity("Objeto", child ? last : kNullEntity);
        MeshRenderer& mr = e.Add<MeshRenderer>();
        mr.mesh = i % 3 == 0 ? sphere : cube;
        mr.isStatic = isStatic;
        e.transform.position = child ? glm::vec3(0, 2, 0) : glm::vec3(pos(rng), size(rng), pos(rng));
        e.transform.scale = glm::vec3(size(rng), size(rng), size(rng));
        e.transform.rotation = glm::quat(glm::vec3(0, angle(rng), 0));
        if (i % 17 == 0) e.active = false;
        last = e.id;
    }
    // Chão grande: fica na lista dos objetos grandes da grade.
    Entity& floor = scene.CreateEntity("Chão");
    floor.Add<MeshRenderer>().mesh = Mesh::CreatePlane();
    floor.Get<MeshRenderer>()->isStatic = isStatic;
    floor.transform.scale = {600, 1, 600};
}
} // namespace

int main() {
    Scene normal, fixed;
    Fill(normal, false);
    Fill(fixed, true);

    SECTION("Os mesmos acertos com e sem estáticos");
    {
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> pos(-250.0f, 250.0f), height(1.0f, 80.0f), dir(-1.0f, 1.0f);
        int hits = 0, same = 0;
        const int rays = 400;
        for (int i = 0; i < rays; ++i) {
            const glm::vec3 origin(pos(rng), height(rng), pos(rng));
            glm::vec3 direction(dir(rng), dir(rng) - 0.3f, dir(rng));
            if (i % 4 == 0) direction = {direction.x * 0.05f, -1.0f, direction.z * 0.05f};  // de cima, como no clique
            const auto a = normal.Raycast(origin, direction);
            const auto b = fixed.Raycast(origin, direction);
            if (a) ++hits;
            if (a.has_value() == b.has_value() && (!a || (a->entity == b->entity && std::abs(a->distance - b->distance) < 1e-4f))) {
                ++same;
            }
        }
        CHECK(same == rays);
        CHECK(hits > rays / 2);
        CHECK(fixed.StaticRenderCount() > 1000);
        CHECK(normal.StaticRenderCount() == 0);
    }

    SECTION("Desativado não é atingido; mover um estático vale depois de MarkRenderDirty");
    {
        Scene scene;
        Entity& box = scene.CreateEntity("Caixa");
        box.Add<MeshRenderer>().mesh = Mesh::CreateCube();
        box.Get<MeshRenderer>()->isStatic = true;
        box.transform.position = {50, 0, 50};
        const EntityId id = box.id;
        auto hit = scene.Raycast({50, 10, 50}, {0, -1, 0});
        CHECK(hit && hit->entity == id && std::abs(hit->distance - 9.5f) < 1e-4f);
        scene.Find(id)->transform.position = {-50, 0, -50};
        scene.MarkRenderDirty();
        CHECK(!scene.Raycast({50, 10, 50}, {0, -1, 0}));
        hit = scene.Raycast({-50, 10, -50}, {0, -1, 0});
        CHECK(hit && hit->entity == id);
        scene.Find(id)->active = false;
        scene.MarkRenderDirty();
        CHECK(!scene.Raycast({-50, 10, -50}, {0, -1, 0}));
    }

    SECTION("O mais perto vence entre estático e não estático");
    {
        Scene scene;
        Entity& low = scene.CreateEntity("Baixo");
        low.Add<MeshRenderer>().mesh = Mesh::CreateCube();
        low.Get<MeshRenderer>()->isStatic = true;
        const EntityId lowId = low.id;
        Entity& high = scene.CreateEntity("Alto");
        high.Add<MeshRenderer>().mesh = Mesh::CreateCube();
        high.transform.position = {0, 3, 0};
        const EntityId highId = high.id;
        auto hit = scene.Raycast({0, 10, 0}, {0, -1, 0});
        CHECK(hit && hit->entity == highId);
        hit = scene.Raycast({0, -10, 0}, {0, 1, 0});
        CHECK(hit && hit->entity == lowId);
    }
    return TestResult();
}
