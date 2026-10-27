// Fase 28: objetos guardados entre quadros (RenderWorld). Sem GPU: registrar, mover, trocar o material e remover.

#include "TestUtil.h"

#include "motor/renderer/Pbr.h"
#include "motor/renderer/RenderWorld.h"

#include <glm/gtc/matrix_transform.hpp>

#include <memory>

using namespace motor;

int main() {
    const std::shared_ptr<const Mesh> cube = Mesh::CreateCube();
    auto red = std::make_shared<Material>();
    red->color = {1, 0, 0, 1};
    auto alsoRed = std::make_shared<Material>(*red);  // outro objeto, os mesmos valores
    auto glass = std::make_shared<Material>();
    glass->blend = BlendMode::Transparent;

    SECTION("Registrar: caixa no mundo e materiais iguais contados uma vez");
    RenderWorld world;
    const RenderWorld::Id a = world.Add({cube, red, glm::translate(glm::mat4(1.0f), {10, 0, 0})});
    const RenderWorld::Id b = world.Add({cube, alsoRed, glm::scale(glm::mat4(1.0f), {2, 2, 2})});
    const RenderWorld::Id c = world.Add({cube, glass, glm::mat4(1.0f), ShadowCasting::Off});
    CHECK(world.Count() == 3);
    CHECK(world.MaterialCount() == 2);
    CHECK(world.Bounds(a).min.x == 9.5f && world.Bounds(a).max.x == 10.5f);
    CHECK(world.Bounds(b).max.y == 1.0f);

    SECTION("Recusados: sem malha, sem material");
    CHECK(world.Add({nullptr, red}) == RenderWorld::kInvalid);
    CHECK(world.Add({cube, nullptr}) == RenderWorld::kInvalid);
    CHECK(world.Count() == 3);

    SECTION("Mover e trocar o material");
    CHECK(world.SetTransform(a, glm::translate(glm::mat4(1.0f), {0, 5, 0})));
    CHECK(world.Bounds(a).min.y == 4.5f);
    CHECK(world.Update(b, {cube, glass, glm::mat4(1.0f)}));
    CHECK(world.Bounds(b).max.y == 0.5f);
    CHECK(world.MaterialCount() == 2);

    SECTION("Remover: os outros continuam achados pelo mesmo id, e o id volta a ser usado");
    world.Remove(a);
    CHECK(!world.Contains(a));
    CHECK(world.Contains(b) && world.Contains(c));
    CHECK(world.Bounds(b).max.y == 0.5f);
    CHECK(!world.SetTransform(a, glm::mat4(1.0f)));
    world.Remove(a);  // de novo: ignorado
    CHECK(world.Count() == 2);
    const RenderWorld::Id d = world.Add({cube, red, glm::mat4(1.0f)});
    CHECK(d == a);
    CHECK(world.Count() == 3);

    SECTION("O material próprio de um componente vai sem dono");
    {
        Material own;
        RenderWorld local;
        const auto id = local.Add({cube, std::shared_ptr<const Material>(std::shared_ptr<const void>(), &own)});
        CHECK(id != RenderWorld::kInvalid);
        local.Clear();
        CHECK(local.Count() == 0 && local.MaterialCount() == 0);
    }

    world.Clear();
    CHECK(world.Count() == 0 && !world.Contains(b));
    return TestResult();
}
