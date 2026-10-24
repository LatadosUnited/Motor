// Navegação (Fase 34): navmesh gerado pela Recast, caminhos pelo Detour, geometria tirada dos Colliders da cena e o
// NavSystem movendo os NavMeshAgent no SceneRuntime. Não usa OpenGL nem janela.
#include "TestUtil.h"

#include <motor/ai/NavMesh.h>
#include <motor/ai/NavSystem.h>
#include <motor/renderer/Mesh.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>

#include <glm/geometric.hpp>

#include <cmath>
#include <vector>

using namespace motor;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// Chão quadrado de lado `size` na altura `y`, virado para cima.
void AddFloor(NavGeometry& g, float size, float y = 0.0f, const glm::vec3& center = glm::vec3(0.0f)) {
    const float h = size * 0.5f;
    const glm::vec3 a = center + glm::vec3(-h, y, h), b = center + glm::vec3(h, y, h);
    const glm::vec3 c = center + glm::vec3(h, y, -h), d = center + glm::vec3(-h, y, -h);
    g.AddTriangle(a, b, c);
    g.AddTriangle(a, c, d);
}

// Caixa fechada (paredes), com os triângulos virados para fora.
void AddBox(NavGeometry& g, const glm::vec3& lo, const glm::vec3& hi) {
    auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 outward) {
        if (glm::dot(glm::cross(b - a, c - a), outward) < 0.0f) std::swap(b, d);
        g.AddTriangle(a, b, c);
        g.AddTriangle(a, c, d);
    };
    const glm::vec3 p[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z},
                            {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}};
    quad(p[0], p[1], p[3], p[2], {0, 0, -1});
    quad(p[4], p[5], p[7], p[6], {0, 0, 1});
    quad(p[0], p[1], p[5], p[4], {0, -1, 0});
    quad(p[2], p[3], p[7], p[6], {0, 1, 0});
    quad(p[0], p[2], p[6], p[4], {-1, 0, 0});
    quad(p[1], p[3], p[7], p[5], {1, 0, 0});
}

bool Near(float a, float b, float eps) { return std::abs(a - b) < eps; }

// Algum trecho do caminho atravessa a caixa (no plano)? Amostra cada trecho em passos de 5 cm.
bool PathCrosses(const std::vector<glm::vec3>& path, const glm::vec2& lo, const glm::vec2& hi) {
    for (size_t i = 1; i < path.size(); ++i) {
        const int samples = std::max(1, static_cast<int>(glm::distance(path[i - 1], path[i]) / 0.05f));
        for (int s = 0; s <= samples; ++s) {
            const glm::vec3 p = glm::mix(path[i - 1], path[i], static_cast<float>(s) / samples);
            if (p.x > lo.x && p.x < hi.x && p.z > lo.y && p.z < hi.y) return true;
        }
    }
    return false;
}

// Cena: chão 20 x 20 (Collider caixa, topo em y = 0), parede de x = -6 a 6 em z = 0, e a superfície.
Scene MakeScene() {
    Scene scene;
    Entity& floor = scene.CreateEntity("Chão");
    floor.transform.position = {0.0f, -0.5f, 0.0f};
    floor.Add<ColliderComponent>().size = {20.0f, 1.0f, 20.0f};

    Entity& wall = scene.CreateEntity("Parede");
    wall.transform.position = {0.0f, 1.5f, 0.0f};
    wall.Add<ColliderComponent>().size = {12.0f, 3.0f, 1.0f};

    scene.CreateEntity("Navegação").Add<NavMeshSurface>();
    return scene;
}

Entity& AddAgent(Scene& scene, const glm::vec3& position) {
    Entity& agent = scene.CreateEntity("Agente");
    agent.transform.position = position;
    agent.Add<NavMeshAgent>();
    return agent;
}

} // namespace

int main() {
    SECTION("Chão liso: navmesh, ponto mais perto e caminho reto");
    {
        NavGeometry g;
        AddFloor(g, 20.0f);
        std::string error;
        auto nav = NavMesh::Build(g, {}, &error);
        CHECK(nav != nullptr);
        if (!nav) std::printf("  erro: %s\n", error.c_str());
        if (nav) {
            CHECK(nav->PolygonCount() > 0);
            const auto sample = nav->SamplePosition({1.0f, 1.5f, 2.0f});
            CHECK(sample.has_value());
            CHECK(sample && Near(sample->y, 0.0f, 0.2f) && Near(sample->x, 1.0f, 0.05f));
            CHECK(!nav->SamplePosition({50.0f, 0.0f, 0.0f}).has_value());  // longe do chão

            std::vector<glm::vec3> path;
            bool partial = true;
            CHECK(nav->FindPath({-5.0f, 0.0f, 0.0f}, {5.0f, 0.0f, 3.0f}, path, &partial));
            CHECK(!partial);
            CHECK(path.size() == 2);  // nada no meio: em linha reta
            CHECK(Near(NavMesh::PathLength(path), std::sqrt(109.0f), 0.1f));
            CHECK(!nav->Raycast({-5.0f, 0.0f, 0.0f}, {5.0f, 0.0f, 0.0f}).has_value());

            // A borda fica a um raio do agente (0,5 m) da beirada do chão (10 m).
            const auto edge = nav->Raycast({0.0f, 0.0f, 0.0f}, {20.0f, 0.0f, 0.0f});
            CHECK(edge.has_value());
            CHECK(edge && Near(edge->point.x, 9.5f, 0.3f));
        }
    }

    SECTION("Parede no meio: o caminho dá a volta");
    {
        NavGeometry g;
        AddFloor(g, 20.0f);
        AddBox(g, {-6.0f, 0.0f, -0.5f}, {6.0f, 3.0f, 0.5f});
        auto nav = NavMesh::Build(g, {});
        CHECK(nav != nullptr);
        if (nav) {
            std::vector<glm::vec3> path;
            CHECK(nav->FindPath({0.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 5.0f}, path));
            CHECK(path.size() > 2);
            CHECK(NavMesh::PathLength(path) > 14.0f);  // reto seriam 10 m; contornando, uns 16
            CHECK(!PathCrosses(path, {-6.0f, -0.5f}, {6.0f, 0.5f}));
            // Cada canto fica a pelo menos ~um raio da parede.
            bool clearance = true;
            for (const glm::vec3& p : path) {
                if (std::abs(p.x) < 6.4f && std::abs(p.z) < 0.9f) clearance = false;
            }
            CHECK(clearance);

            const auto hit = nav->Raycast({0.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 5.0f});
            CHECK(hit.has_value());
            CHECK(hit && hit->point.z < -0.5f && hit->point.z > -1.5f);
            CHECK(hit && hit->fraction > 0.3f && hit->fraction < 0.5f);
        }
    }

    SECTION("Duas ilhas: caminho parcial até a borda mais perto");
    {
        NavGeometry g;
        AddFloor(g, 6.0f, 0.0f, {-5.0f, 0.0f, 0.0f});
        AddFloor(g, 6.0f, 0.0f, {5.0f, 0.0f, 0.0f});
        auto nav = NavMesh::Build(g, {});
        CHECK(nav != nullptr);
        if (nav) {
            std::vector<glm::vec3> path;
            bool partial = false;
            CHECK(nav->FindPath({-5.0f, 0.0f, 0.0f}, {5.0f, 0.0f, 0.0f}, path, &partial));
            CHECK(partial);
            CHECK(!path.empty() && path.back().x < -1.0f);  // para na ilha de onde saiu
        }
    }

    SECTION("Rampa: sobe se for suave, não sobe se for íngreme");
    {
        // Chão em y = 0 de x = -10 a 0 e uma rampa de x = 0 a 10, subindo `h`, que termina num platô.
        auto build = [](float h) {
            NavGeometry g;
            g.AddTriangle({-10, 0, 3}, {0, 0, 3}, {0, 0, -3});
            g.AddTriangle({-10, 0, 3}, {0, 0, -3}, {-10, 0, -3});
            g.AddTriangle({0, 0, 3}, {10, h, 3}, {10, h, -3});
            g.AddTriangle({0, 0, 3}, {10, h, -3}, {0, 0, -3});
            g.AddTriangle({10, h, 3}, {20, h, 3}, {20, h, -3});
            g.AddTriangle({10, h, 3}, {20, h, -3}, {10, h, -3});
            return NavMesh::Build(g, {});
        };
        auto gentle = build(3.0f);   // ~17 graus
        auto steep = build(15.0f);   // ~56 graus, acima dos 45 do agente
        std::vector<glm::vec3> path;
        bool partial = true;
        CHECK(gentle && gentle->FindPath({-5, 0, 0}, {15, 3, 0}, path, &partial) && !partial);
        CHECK(!path.empty() && Near(path.back().y, 3.0f, 0.3f));
        partial = false;
        CHECK(steep && !steep->SamplePosition({5, 7.5f, 0}, {1, 1, 1}).has_value());  // a rampa não é chão
        CHECK(steep && steep->FindPath({-5, 0, 0}, {15, 15, 0}, path, &partial) && partial);
    }

    SECTION("Sem triângulos ou de cabeça para baixo: não gera, e diz por quê");
    {
        std::string error;
        CHECK(NavMesh::Build({}, {}, &error) == nullptr);
        CHECK(!error.empty());
        NavGeometry upsideDown;
        upsideDown.AddTriangle({-5, 0, 5}, {5, 0, -5}, {5, 0, 5});  // normal para baixo
        upsideDown.AddTriangle({-5, 0, 5}, {-5, 0, -5}, {5, 0, -5});
        error.clear();
        CHECK(NavMesh::Build(upsideDown, {}, &error) == nullptr);
        CHECK(!error.empty());
    }

    SECTION("Geometria da cena: só Colliders estáticos de entidades ativas");
    {
        Scene scene = MakeScene();
        const size_t base = CollectNavGeometry(scene).TriangleCount();
        CHECK(base == 24);  // duas caixas

        Entity& falling = scene.CreateEntity("Caixa que cai");
        falling.Add<ColliderComponent>();
        falling.Add<RigidbodyComponent>();
        Entity& trigger = scene.CreateEntity("Gatilho");
        trigger.Add<ColliderComponent>().isTrigger = true;
        Entity& off = scene.CreateEntity("Desligada");
        off.Add<ColliderComponent>();
        off.active = false;
        Entity& walker = AddAgent(scene, {0, 0, 0});
        walker.Add<ColliderComponent>();
        CHECK(CollectNavGeometry(scene).TriangleCount() == base);

        Entity& column = scene.CreateEntity("Coluna");
        column.Add<ColliderComponent>().shape = ColliderShape::Cylinder;
        CHECK(CollectNavGeometry(scene).TriangleCount() > base);

        // Collider de malha usa a malha do MeshRenderer, com a escala e a posição da entidade.
        Scene planeScene;
        Entity& plane = planeScene.CreateEntity("Plano");
        plane.transform.position = {3.0f, 1.0f, 0.0f};
        plane.transform.scale = {10.0f, 1.0f, 10.0f};
        plane.Add<MeshRenderer>().mesh = Mesh::CreatePlane();
        plane.Add<ColliderComponent>().shape = ColliderShape::Mesh;
        const NavGeometry g = CollectNavGeometry(planeScene);
        CHECK(g.TriangleCount() == 2);
        auto nav = NavMesh::Build(g, {});
        CHECK(nav != nullptr);
        const auto sample = nav ? nav->SamplePosition({3.0f, 1.0f, 0.0f}) : std::nullopt;
        CHECK(sample && Near(sample->y, 1.0f, 0.2f));
        CHECK(nav && !nav->SamplePosition({-4.0f, 1.0f, 0.0f}, {0.2f, 0.5f, 0.2f}).has_value());
    }

    SECTION("Agente no Play: contorna a parede e chega");
    {
        Scene scene = MakeScene();
        const EntityId id = AddAgent(scene, {0.0f, 0.0f, -5.0f}).id;
        SceneRuntime runtime(scene);
        runtime.Start();
        NavSystem* nav = runtime.GetSystem<NavSystem>();
        CHECK(nav != nullptr);
        if (nav) {
            CHECK(nav->GetNavMesh() != nullptr);
            CHECK(nav->Stats().polygons > 0 && nav->Stats().triangles == 24);
            CHECK(nav->SetDestination(id, {0.0f, 0.0f, 5.0f}));
            CHECK(nav->HasPath(id));
            CHECK(nav->RemainingDistance(id) > 14.0f);

            bool hitWall = false;
            float topSpeed = 0.0f;
            for (int i = 0; i < 60 * 15 && !nav->HasArrived(id); ++i) {
                runtime.FixedUpdate(kDt);
                runtime.Update(kDt);
                const glm::vec3 p = scene.Find(id)->transform.position;
                if (std::abs(p.x) < 6.0f && std::abs(p.z) < 0.5f) hitWall = true;
                topSpeed = std::max(topSpeed, glm::length(nav->GetVelocity(id)));
            }
            const glm::vec3 end = scene.Find(id)->transform.position;
            CHECK(nav->HasArrived(id));
            CHECK(!nav->HasPath(id));
            CHECK(!hitWall);
            CHECK(glm::distance(end, glm::vec3(0.0f, 0.0f, 5.0f)) < 0.3f);
            CHECK(Near(end.y, 0.0f, 0.2f));
            CHECK(topSpeed <= 3.5f + 1e-3f && topSpeed > 3.0f);  // velocidade do componente

            // Virou para onde andou por último (do canto da parede até o destino, em diagonal para +Z): a frente
            // (-Z local) aponta para +Z no mundo.
            const glm::vec3 forward = scene.Find(id)->transform.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
            CHECK(forward.z > 0.3f);

            // Parado depois de chegar: a velocidade cai a zero em poucos passos.
            for (int i = 0; i < 30; ++i) runtime.FixedUpdate(kDt);
            CHECK(glm::length(nav->GetVelocity(id)) < 1e-3f);

            CHECK(!nav->SetDestination(id, {80.0f, 0.0f, 0.0f}));  // longe do navmesh
            CHECK(nav->Warp(id, {7.0f, 3.0f, 7.0f}));
            CHECK(glm::distance(scene.Find(id)->transform.position, glm::vec3(7.0f, 0.0f, 7.0f)) < 0.2f);
        }
        runtime.Stop();
    }

    SECTION("Agente com alvo: segue a entidade quando ela anda");
    {
        Scene scene = MakeScene();
        Entity& target = scene.CreateEntity("Alvo");
        target.transform.position = {-8.0f, 0.0f, 8.0f};
        const EntityId targetId = target.id;
        Entity& agentEntity = AddAgent(scene, {-8.0f, 0.0f, -8.0f});
        agentEntity.Get<NavMeshAgent>()->target = EntityRef(targetId);
        agentEntity.Get<NavMeshAgent>()->stoppingDistance = 1.0f;
        const EntityId id = agentEntity.id;

        SceneRuntime runtime(scene);
        runtime.Start();
        NavSystem* nav = runtime.GetSystem<NavSystem>();
        for (int i = 0; i < 60 * 8; ++i) runtime.FixedUpdate(kDt);
        CHECK(nav && nav->HasArrived(id));
        CHECK(glm::distance(scene.Find(id)->transform.position, glm::vec3(-8.0f, 0.0f, 8.0f)) < 1.2f);

        // O alvo vai para o outro lado da parede: o agente refaz o caminho e vai atrás.
        scene.Find(targetId)->transform.position = {8.0f, 0.0f, -8.0f};
        for (int i = 0; i < 60 * 10; ++i) runtime.FixedUpdate(kDt);
        CHECK(glm::distance(scene.Find(id)->transform.position, glm::vec3(8.0f, 0.0f, -8.0f)) < 1.2f);
        runtime.Stop();
    }

    SECTION("Agente filho de outra entidade e com altura da base");
    {
        Scene scene = MakeScene();
        Entity& parent = scene.CreateEntity("Grupo");
        parent.transform.position = {2.0f, 0.0f, 0.0f};
        const EntityId parentId = parent.id;
        Entity& agentEntity = AddAgent(scene, {0.0f, 1.0f, -5.0f});
        agentEntity.Get<NavMeshAgent>()->baseOffset = 1.0f;
        const EntityId id = agentEntity.id;
        scene.SetParent(id, parentId, false);  // posição local (0, 1, -5) => mundo (2, 1, -5)

        SceneRuntime runtime(scene);
        runtime.Start();
        NavSystem* nav = runtime.GetSystem<NavSystem>();
        CHECK(nav && nav->SetDestination(id, {8.0f, 0.0f, -5.0f}));
        for (int i = 0; i < 60 * 5 && nav && !nav->HasArrived(id); ++i) runtime.FixedUpdate(kDt);
        const glm::vec3 world = glm::vec3(scene.WorldMatrix(id)[3]);
        CHECK(nav && nav->HasArrived(id));
        CHECK(glm::distance(world, glm::vec3(8.0f, 1.0f, -5.0f)) < 0.3f);  // os pés no chão, a origem 1 m acima
        CHECK(Near(scene.Find(id)->transform.position.x, 6.0f, 0.3f));      // local: relativo ao pai
        runtime.Stop();
    }

    SECTION("Sem Superfície de navegação: sem navmesh, e o agente fica parado");
    {
        Scene scene;
        Entity& floor = scene.CreateEntity("Chão");
        floor.Add<ColliderComponent>().size = {20.0f, 1.0f, 20.0f};
        const EntityId id = AddAgent(scene, {0.0f, 0.5f, 0.0f}).id;
        SceneRuntime runtime(scene);
        runtime.Start();
        NavSystem* nav = runtime.GetSystem<NavSystem>();
        CHECK(nav && nav->GetNavMesh() == nullptr);
        CHECK(nav && !nav->SetDestination(id, {5.0f, 0.5f, 0.0f}));
        for (int i = 0; i < 30; ++i) runtime.FixedUpdate(kDt);
        CHECK(glm::distance(scene.Find(id)->transform.position, glm::vec3(0.0f, 0.5f, 0.0f)) < 1e-4f);
        CHECK(nav && !nav->Rebuild());
        runtime.Stop();
    }

    return TestResult();
}
