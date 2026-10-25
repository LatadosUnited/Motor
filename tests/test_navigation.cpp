// Navegação (Fase 34): navmesh gerado pela Recast, caminhos pelo Detour, geometria tirada dos Colliders da cena e o
// NavSystem movendo os NavMeshAgent no SceneRuntime, e o navmesh salvo em .mnav (Tarefa 7). Não usa OpenGL nem
// janela. Fase 35: a multidão (DetourCrowd), com agentes que desviam uns dos outros.
#include "TestUtil.h"

#include <motor/ai/NavMesh.h>
#include <motor/ai/NavSystem.h>
#include <motor/assets/AssetLibrary.h>
#include <motor/assets/Model.h>
#include <motor/core/Paths.h>
#include <motor/physics/PhysicsSystem.h>
#include <motor/project/Project.h>
#include <motor/renderer/Mesh.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/scene/SceneSerializer.h>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
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

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    const fs::path base = fs::absolute(TestDir(argc, argv, "test_navigation_tmp")).lexically_normal();

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

            // A versão sobe a cada navmesh novo: o viewport do editor refaz o desenho só quando ela muda.
            const uint64_t version = nav->Version();
            CHECK(version > 0);
            runtime.FixedUpdate(kDt);
            CHECK(nav->Version() == version);
            CHECK(nav->Rebuild() && nav->Version() == version + 1);

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

    SECTION("Cantos: o agente não passa do canto embalado nem volta até ele");
    {
        // A cena que mostrou o problema no editor (chão e parede de cubos escalados, agente seguindo um alvo): o
        // agente virava aos poucos, passava do canto da parede e voltava uns 70 cm até ele.
        Scene scene;
        Entity& floor = scene.CreateEntity("Chão");
        floor.transform.scale = {40.0f, 1.0f, 40.0f};
        floor.Add<ColliderComponent>().size = {1.0f, 0.02f, 1.0f};
        Entity& wall = scene.CreateEntity("Parede");
        wall.transform.position = {0.0f, 1.5f, 0.0f};
        wall.transform.scale = {12.0f, 3.0f, 1.0f};
        wall.Add<ColliderComponent>();
        scene.CreateEntity("Navegação").Add<NavMeshSurface>();
        Entity& target = scene.CreateEntity("Alvo");
        target.transform.position = {0.0f, 0.5f, 6.0f};
        const EntityId targetId = target.id;
        Entity& a = AddAgent(scene, {0.0f, 1.0f, -6.0f});
        a.Get<NavMeshAgent>()->baseOffset = 1.0f;
        a.Get<NavMeshAgent>()->stoppingDistance = 1.0f;
        a.Get<NavMeshAgent>()->target = EntityRef(targetId);
        const EntityId id = a.id;
        SceneRuntime runtime(scene);
        runtime.Start();
        NavSystem* nav = runtime.GetSystem<NavSystem>();
        // O caminho até o alvo nasce no primeiro passo: compara a partir do segundo.
        float previous = -1.0f;
        float worstIncrease = 0.0f;
        int steps = 0;
        for (; steps < 60 * 10 && nav && !nav->HasArrived(id); ++steps) {
            runtime.FixedUpdate(kDt);
            const float remaining = nav->RemainingDistance(id);
            if (nav->HasPath(id) && previous >= 0.0f) worstIncrease = std::max(worstIncrease, remaining - previous);
            previous = nav->HasPath(id) ? remaining : -1.0f;
        }
        CHECK(nav && nav->HasArrived(id));
        CHECK(worstIncrease < 0.01f);  // o que falta só diminui
        // ~17 m a 3,5 m/s: uns 5 s; com o vaivém nos cantos passava de 6,7 s.
        CHECK(steps * kDt < 5.6f);
        std::printf("  chegou em %.2f s (maior aumento do que falta: %.3f m)\n", steps * kDt, worstIncrease);
        runtime.Stop();
    }

    SECTION("Multidão: dois agentes se cruzam sem se atravessar");
    {
        // Lado a lado na mesma linha, trocando de lugar: sem desvio, um passaria por dentro do outro no meio.
        Scene scene = MakeScene();
        const EntityId a = AddAgent(scene, {-7.0f, 0.0f, 5.0f}).id;
        const EntityId b = AddAgent(scene, {7.0f, 0.0f, 5.0f}).id;
        SceneRuntime runtime(scene);
        runtime.Start();
        NavSystem* nav = runtime.GetSystem<NavSystem>();
        CHECK(nav && nav->SetDestination(a, {7.0f, 0.0f, 5.0f}) && nav->SetDestination(b, {-7.0f, 0.0f, 5.0f}));
        float closest = 1e9f;
        bool sawEachOther = false;
        int steps = 0;
        for (; steps < 60 * 15 && nav && !(nav->HasArrived(a) && nav->HasArrived(b)); ++steps) {
            runtime.FixedUpdate(kDt);
            closest = std::min(closest, glm::distance(scene.Find(a)->transform.position, scene.Find(b)->transform.position));
            const std::vector<EntityId> na = nav->GetNeighbors(a), nb = nav->GetNeighbors(b);
            if (na == std::vector<EntityId>{b} && nb == std::vector<EntityId>{a}) sawEachOther = true;
        }
        CHECK(nav && nav->HasArrived(a) && nav->HasArrived(b));
        CHECK(sawEachOther);  // cada um é o vizinho de quem o outro desvia
        CHECK(closest > 0.9f);  // raios de 0,5 m: nunca mais perto que 1 m, com folga de 10 cm
        CHECK(glm::distance(scene.Find(a)->transform.position, glm::vec3(7.0f, 0.0f, 5.0f)) < 0.3f);
        std::printf("  cruzaram em %.2f s, a %.2f m um do outro no mais perto\n", steps * kDt, closest);
        runtime.Stop();
    }


    SECTION("Multidão: muitos agentes, a multidão cresce e ninguém entra no outro");
    {
        // 50 agentes do lado de -Z vão todos para o mesmo ponto do outro lado da parede; depois que saem, nascem mais
        // 60 onde eles estavam (a multidão começa com 100 lugares e dobra).
        Scene scene = MakeScene();
        std::vector<EntityId> agents;
        for (int i = 0; i < 50; ++i) {
            agents.push_back(AddAgent(scene, {-7.0f + (i % 10) * 1.5f, 0.0f, -3.0f - (i / 10) * 1.4f}).id);
        }
        SceneRuntime runtime(scene);
        runtime.Start();
        NavSystem* nav = runtime.GetSystem<NavSystem>();
        const glm::vec3 goal(0.0f, 0.0f, 6.0f);
        int planned = 0;
        for (EntityId id : agents) planned += nav && nav->SetDestination(id, goal) ? 1 : 0;
        CHECK(planned == 50);

        double worstMs = 0.0;
        for (int step = 0; step < 60 * 20 && nav; ++step) {
            if (step == 150) {
                for (int i = 0; i < 60; ++i) {
                    Entity& late = AddAgent(scene, {-7.0f + (i % 10) * 1.5f, 0.0f, -3.0f - (i / 10) * 1.1f});
                    late.Get<NavMeshAgent>()->target = EntityRef(agents[0]);  // os novos seguem o primeiro (fila)
                    late.Get<NavMeshAgent>()->stoppingDistance = 2.0f;
                    agents.push_back(late.id);
                }
            }
            const auto t0 = std::chrono::steady_clock::now();
            runtime.FixedUpdate(kDt);
            worstMs = std::max(worstMs, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }

        // Ninguém dentro de ninguém (raios de 0,5 m; a colisão da multidão deixa entrar um pouco), e todos andaram.
        float closest = 1e9f;
        int moved = 0;
        int behindWall = 0;
        for (size_t i = 0; i < agents.size(); ++i) {
            const glm::vec3 p = scene.Find(agents[i])->transform.position;
            for (size_t j = i + 1; j < agents.size(); ++j) {
                closest = std::min(closest, glm::distance(p, scene.Find(agents[j])->transform.position));
            }
            if (i < 50 && p.z > 0.5f) ++moved;
            if (p.z < 0.0f) ++behindWall;
        }
        CHECK(agents.size() == 110);
        CHECK(closest > 0.6f);
        CHECK(moved == 50);           // os 50 primeiros passaram para o lado do ponto
        CHECK(behindWall < 10);       // os 60 novos também deram a volta na parede atrás do primeiro
        std::printf("  110 agentes: o mais perto a %.2f m, passo mais lento %.1f ms\n", closest, worstMs);
        runtime.Stop();
    }

    SECTION("Multidão: agente que um script tirou do lugar continua de onde foi posto");
    {
        Scene scene = MakeScene();
        const EntityId id = AddAgent(scene, {-8.0f, 0.0f, -8.0f}).id;
        SceneRuntime runtime(scene);
        runtime.Start();
        NavSystem* nav = runtime.GetSystem<NavSystem>();
        CHECK(nav && nav->SetDestination(id, {8.0f, 0.0f, 8.0f}));
        for (int i = 0; i < 30; ++i) runtime.FixedUpdate(kDt);
        scene.Find(id)->transform.position = {8.0f, 0.0f, -8.0f};  // como um script faria
        runtime.FixedUpdate(kDt);
        CHECK(glm::distance(scene.Find(id)->transform.position, glm::vec3(8.0f, 0.0f, -8.0f)) < 0.2f);
        for (int i = 0; i < 60 * 10 && nav && !nav->HasArrived(id); ++i) runtime.FixedUpdate(kDt);
        CHECK(nav && nav->HasArrived(id));
        CHECK(glm::distance(scene.Find(id)->transform.position, glm::vec3(8.0f, 0.0f, 8.0f)) < 0.3f);
        runtime.Stop();
    }

    SECTION("Agente com Controlador de personagem: anda pela física, empurra a caixa e não sai do navmesh");
    {
        // A caixa tem Rigidbody: não entra no navmesh, e o caminho passa por ela. Quem a tira do caminho é a física.
        Scene scene = MakeScene();
        Entity& box = scene.CreateEntity("Caixa");
        box.transform.position = {0.0f, 0.5f, 5.25f};
        box.Add<ColliderComponent>();
        box.Add<RigidbodyComponent>().mass = 5.0f;
        const EntityId boxId = box.id;
        Entity& agentEntity = AddAgent(scene, {-6.0f, 0.9f, 5.0f});  // centro da cápsula de 1,8 m
        agentEntity.Add<CharacterControllerComponent>();
        const EntityId id = agentEntity.id;
        SceneRuntime runtime(scene);
        runtime.Start();
        NavSystem* nav = runtime.GetSystem<NavSystem>();
        PhysicsSystem* physics = runtime.GetSystem<PhysicsSystem>();
        CHECK(nav && physics && nav->SetDestination(id, {6.0f, 0.0f, 5.0f}));
        float worstOff = 0.0f;
        int steps = 0;
        for (; steps < 60 * 12 && nav && !nav->HasArrived(id); ++steps) {
            runtime.FixedUpdate(kDt);
            const glm::vec3 feet = scene.Find(id)->transform.position - glm::vec3(0.0f, 0.9f, 0.0f);
            const auto onMesh = nav->SamplePosition(feet, 1.0f);
            worstOff = std::max(worstOff, onMesh ? glm::length(glm::vec2(onMesh->x - feet.x, onMesh->z - feet.z)) : 9.0f);
        }
        const glm::vec3 end = scene.Find(id)->transform.position;
        CHECK(nav && nav->HasArrived(id));
        CHECK(glm::length(glm::vec2(end.x - 6.0f, end.z - 5.0f)) < 0.5f);
        CHECK(Near(end.y, 0.9f, 0.1f) && physics && physics->IsGrounded(id));  // em pé no chão, pela física
        CHECK(Near(NavAgentFeet(scene, id).y, 0.0f, 0.1f));                    // pés na base da cápsula
        CHECK(worstOff < 0.15f);                                               // os pés sempre no navmesh
        const glm::vec3 boxEnd = scene.Find(boxId)->transform.position;
        CHECK(glm::distance(boxEnd, glm::vec3(0.0f, 0.5f, 5.25f)) > 0.2f);    // a caixa foi empurrada
        std::printf("  com física: chegou em %.2f s; caixa em (%.2f, %.2f); fora do navmesh no máximo %.3f m\n",
                    steps * kDt, boxEnd.x, boxEnd.z, worstOff);

        // Empurrado para fora (como por uma explosão): volta para o navmesh e continua.
        CHECK(nav && nav->SetDestination(id, {-6.0f, 0.0f, 5.0f}));
        scene.Find(id)->transform.position = {0.0f, 0.9f, 9.9f};  // além da borda (navmesh vai até ~9,5)
        for (int i = 0; i < 60 * 12 && nav && !nav->HasArrived(id); ++i) runtime.FixedUpdate(kDt);
        const glm::vec3 back = scene.Find(id)->transform.position;
        CHECK(nav && nav->HasArrived(id));
        CHECK(glm::length(glm::vec2(back.x + 6.0f, back.z - 5.0f)) < 0.5f);
        runtime.Stop();
    }

    SECTION("Ligação de navegação: pulo sobre o vão até a plataforma mais alta");
    {
        // Plataforma A (topo em y = 0, x de -9 a -1) e B (topo em y = 1, x de 1 a 9): 2 m de vão, só pulando.
        auto makeScene = [](bool withLink, bool bothWays) {
            Scene scene;
            Entity& a = scene.CreateEntity("A");
            a.transform.position = {-5.0f, -0.5f, 0.0f};
            a.Add<ColliderComponent>().size = {8.0f, 1.0f, 8.0f};
            Entity& b = scene.CreateEntity("B");
            b.transform.position = {5.0f, 0.5f, 0.0f};
            b.Add<ColliderComponent>().size = {8.0f, 1.0f, 8.0f};
            scene.CreateEntity("Navegação").Add<NavMeshSurface>();
            if (withLink) {
                Entity& link = scene.CreateEntity("Pulo");
                link.transform.position = {0.0f, 0.0f, 1.0f};  // pontas relativas à entidade
                NavMeshLink& l = link.Add<NavMeshLink>();
                l.start = {-1.6f, 0.0f, -1.0f};
                l.end = {1.6f, 1.0f, -1.0f};
                l.bidirectional = bothWays;
            }
            return scene;
        };

        // Sem a ligação, o outro lado é outra ilha: caminho parcial até a borda.
        {
            Scene scene = makeScene(false, true);
            const EntityId id = AddAgent(scene, {-6.0f, 0.0f, 0.0f}).id;
            SceneRuntime runtime(scene);
            runtime.Start();
            NavSystem* nav = runtime.GetSystem<NavSystem>();
            CHECK(nav && nav->SetDestination(id, {6.0f, 1.0f, 0.0f}) && nav->IsPathPartial(id));
            runtime.Stop();
        }

        Scene scene = makeScene(true, false);
        CHECK(CollectNavGeometry(scene).links.size() == 1);
        const EntityId id = AddAgent(scene, {-6.0f, 0.0f, 0.0f}).id;
        SceneRuntime runtime(scene);
        runtime.Start();
        NavSystem* nav = runtime.GetSystem<NavSystem>();
        CHECK(nav && nav->GetNavMesh() && nav->GetNavMesh()->Links().size() == 1);
        CHECK(nav && nav->SetDestination(id, {6.0f, 1.0f, 0.0f}) && !nav->IsPathPartial(id));
        bool jumped = false;
        float top = 0.0f;
        int steps = 0;
        for (; steps < 60 * 10 && nav && !nav->HasArrived(id); ++steps) {
            runtime.FixedUpdate(kDt);
            if (nav->IsOnLink(id)) {
                jumped = true;
                CHECK(nav->HasPath(id));
            }
            const glm::vec3 p = scene.Find(id)->transform.position;
            if (p.x > -1.0f && p.x < 1.0f) top = std::max(top, p.y);  // em cima do vão
        }
        const glm::vec3 end = scene.Find(id)->transform.position;
        CHECK(nav && nav->HasArrived(id) && jumped);
        CHECK(glm::distance(end, glm::vec3(6.0f, 1.0f, 0.0f)) < 0.3f);
        CHECK(top > 1.3f);  // arco de 1 m acima da linha entre as pontas (no meio dela, y = 0,5)
        std::printf("  pulou e chegou em %.2f s, no alto do vão a y = %.2f\n", steps * kDt, top);

        // Só de A para B: a volta não tem caminho (parcial até a borda de B).
        CHECK(nav && nav->SetDestination(id, {-6.0f, 0.0f, 0.0f}) && nav->IsPathPartial(id));
        runtime.Stop();

        // Com corpo físico, a física segue o arco e o personagem cai em pé do outro lado.
        Scene physical = makeScene(true, true);
        Entity& body = AddAgent(physical, {-6.0f, 0.9f, 0.0f});
        body.Add<CharacterControllerComponent>();
        const EntityId bodyId = body.id;
        SceneRuntime physicalRuntime(physical);
        physicalRuntime.Start();
        NavSystem* nav2 = physicalRuntime.GetSystem<NavSystem>();
        CHECK(nav2 && nav2->SetDestination(bodyId, {6.0f, 1.0f, 0.0f}));
        for (int i = 0; i < 60 * 10 && nav2 && !nav2->HasArrived(bodyId); ++i) physicalRuntime.FixedUpdate(kDt);
        const glm::vec3 landed = physical.Find(bodyId)->transform.position;
        CHECK(nav2 && nav2->HasArrived(bodyId));
        CHECK(glm::distance(landed, glm::vec3(6.0f, 1.9f, 0.0f)) < 0.5f);
        physicalRuntime.Stop();

        // Altura do arco 0 (escada): vai reto de uma ponta à outra, sem passar acima da linha entre elas.
        Scene ladder = makeScene(true, true);
        ladder.FindByName("Pulo")->Get<NavMeshLink>()->arcHeight = 0.0f;
        const EntityId climber = AddAgent(ladder, {-6.0f, 0.0f, 0.0f}).id;
        SceneRuntime ladderRuntime(ladder);
        ladderRuntime.Start();
        NavSystem* nav3 = ladderRuntime.GetSystem<NavSystem>();
        CHECK(nav3 && nav3->SetDestination(climber, {6.0f, 1.0f, 0.0f}));
        float above = -1.0f;
        for (int i = 0; i < 60 * 10 && nav3 && !nav3->HasArrived(climber); ++i) {
            ladderRuntime.FixedUpdate(kDt);
            const glm::vec3 p = ladder.Find(climber)->transform.position;
            if (nav3->IsOnLink(climber) && p.x > -1.6f && p.x < 1.6f) {
                above = std::max(above, p.y - (p.x + 1.6f) / 3.2f);  // acima da linha (y de 0 a 1 entre as pontas)
            }
        }
        CHECK(nav3 && nav3->HasArrived(climber));
        CHECK(above >= 0.0f && above < 0.05f);
        ladderRuntime.Stop();

        // A ligação entra na impressão digital e vai junto no .mnav.
        const NavGeometry withLink = CollectNavGeometry(makeScene(true, true));
        NavGeometry withoutLink = withLink;
        withoutLink.links.clear();
        CHECK(NavMesh::SourceHash(withLink, {}) != NavMesh::SourceHash(withoutLink, {}));
        auto built = NavMesh::Build(withLink, {});
        const fs::path file = base / "ligacao.mnav";
        CHECK(built && built->Save(file));
        auto loaded = NavMesh::Load(file);
        CHECK(loaded && loaded->Links().size() == 1 && loaded->Links()[0].bidirectional);
    }

    SECTION("Áreas com custo: dá a volta na lama cara, atravessa a barata, e quem não anda nela nem entra");
    {
        // Chão 20 x 20 sem parede; faixa de lama (área 2) de x = -2 a 2 e z = -10 a 6: a volta pela ponta livre
        // (z > 6) tem uns 25 m, atravessar tem 16 m, 4 deles na lama.
        auto makeScene = [](float mudLength) {
            Scene scene;
            Entity& floor = scene.CreateEntity("Chão");
            floor.transform.position = {0.0f, -0.5f, 0.0f};
            floor.Add<ColliderComponent>().size = {20.0f, 1.0f, 20.0f};
            scene.CreateEntity("Navegação").Add<NavMeshSurface>();
            Entity& mud = scene.CreateEntity("Lama");
            mud.transform.position = {0.0f, 0.0f, -10.0f + mudLength * 0.5f};
            NavMeshArea& area = mud.Add<NavMeshArea>();
            area.size = {4.0f, 2.0f, mudLength};
            area.area = 2;
            area.name = "Lama";
            area.cost = 5.0f;
            return scene;
        };
        auto maxZ = [](const std::vector<glm::vec3>& corners) {
            float z = -1e9f;
            for (const glm::vec3& c : corners) z = std::max(z, c.z);
            return z;
        };

        Scene scene = makeScene(16.0f);
        CHECK(CollectNavGeometry(scene).areas.size() == 1);
        const EntityId mudId = scene.FindByName("Lama")->id;
        const EntityId id = AddAgent(scene, {-8.0f, 0.0f, -5.0f}).id;
        SceneRuntime runtime(scene);
        runtime.Start();
        NavSystem* nav = runtime.GetSystem<NavSystem>();
        const NavMesh* mesh = nav ? nav->GetNavMesh() : nullptr;
        CHECK(mesh && mesh->AreaAt({0.0f, 0.0f, -5.0f}) == 2 && mesh->AreaAt({-8.0f, 0.0f, -5.0f}) == 0);
        runtime.FixedUpdate(kDt);  // os custos da cena chegam ao navmesh
        CHECK(mesh && Near(mesh->AreaCost(2), 5.0f, 1e-4f) && Near(mesh->AreaCost(0), 1.0f, 1e-4f));

        // Custo 5: atravessar vale 12 + 4 * 5 = 32 m, mais que a volta.
        std::vector<glm::vec3> path;
        CHECK(nav && nav->FindPath({-8.0f, 0.0f, -5.0f}, {8.0f, 0.0f, -5.0f}, path) && maxZ(path) > 5.5f);
        // Custo 1,5 (mudado no jogo, sem gerar de novo): 18 m, menos que a volta.
        scene.Find(mudId)->Get<NavMeshArea>()->cost = 1.5f;
        runtime.FixedUpdate(kDt);
        CHECK(nav && nav->FindPath({-8.0f, 0.0f, -5.0f}, {8.0f, 0.0f, -5.0f}, path) && maxZ(path) < -4.0f);

        // Andando de verdade (multidão), com a lama cara de novo: o agente dá a volta e não corta por ela no caminho
        // (o atalho de visão do DetourCrowd ignorava o custo e desfazia a volta).
        scene.Find(mudId)->Get<NavMeshArea>()->cost = 5.0f;
        CHECK(nav && nav->SetDestination(id, {8.0f, 0.0f, -5.0f}));
        bool cut = false;
        for (int i = 0; i < 60 * 15 && nav && !nav->HasArrived(id); ++i) {
            runtime.FixedUpdate(kDt);
            const glm::vec3 p = scene.Find(id)->transform.position;
            if (std::abs(p.x) < 1.9f && p.z < 5.9f) cut = true;
        }
        CHECK(nav && nav->HasArrived(id) && !cut);
        CHECK(nav && nav->Warp(id, {-8.0f, 0.0f, -5.0f}));
        scene.Find(mudId)->Get<NavMeshArea>()->cost = 1.5f;

        // O agente que não anda na lama dá a volta mesmo com ela barata, e nunca põe o pé nela.
        scene.Find(id)->Get<NavMeshAgent>()->areas.bits = static_cast<uint16_t>(0xffff & ~(1u << 2));
        CHECK(nav && nav->SetDestination(id, {8.0f, 0.0f, -5.0f}));
        bool stepped = false;
        for (int i = 0; i < 60 * 15 && nav && !nav->HasArrived(id); ++i) {
            runtime.FixedUpdate(kDt);
            const glm::vec3 p = scene.Find(id)->transform.position;
            if (std::abs(p.x) < 1.9f && p.z < 5.9f) stepped = true;
        }
        CHECK(nav && nav->HasArrived(id));
        CHECK(!stepped);
        CHECK(glm::distance(scene.Find(id)->transform.position, glm::vec3(8.0f, 0.0f, -5.0f)) < 0.3f);
        // Pelo FindPath com a mesma máscara, o caminho também dá a volta.
        CHECK(nav && nav->FindPath({-8.0f, 0.0f, -5.0f}, {8.0f, 0.0f, -5.0f}, path, 0xffff & ~(1u << 2)) && maxZ(path) > 5.5f);
        runtime.Stop();

        // Lama de ponta a ponta: quem não anda nela não tem caminho até o outro lado.
        Scene closed = makeScene(20.0f);
        Entity& walker = AddAgent(closed, {-8.0f, 0.0f, -5.0f});
        walker.Get<NavMeshAgent>()->areas.bits = static_cast<uint16_t>(0xffff & ~(1u << 2));
        const EntityId walkerId = walker.id;
        SceneRuntime closedRuntime(closed);
        closedRuntime.Start();
        NavSystem* nav2 = closedRuntime.GetSystem<NavSystem>();
        CHECK(nav2 && nav2->SetDestination(walkerId, {8.0f, 0.0f, -5.0f}) && nav2->IsPathPartial(walkerId));
        closedRuntime.Stop();

        // A área entra na impressão digital e volta do .mnav.
        NavGeometry withArea = CollectNavGeometry(makeScene(16.0f));
        NavGeometry withoutArea = withArea;
        withoutArea.areas.clear();
        CHECK(NavMesh::SourceHash(withArea, {}) != NavMesh::SourceHash(withoutArea, {}));
        auto built = NavMesh::Build(withArea, {});
        const fs::path file = base / "areas.mnav";
        CHECK(built && built->Save(file));
        auto loaded = NavMesh::Load(file);
        CHECK(loaded && loaded->AreaAt({0.0f, 0.0f, -5.0f}) == 2 && loaded->AreaAt({0.0f, 0.0f, 8.0f}) == 0);
    }

    SECTION("Animação do agente: parado, andando, correndo e pulando pela velocidade");
    {
        // Modelo sem malha com quatro clipes vazios: basta para o Animator trocar de clipe.
        auto model = std::make_shared<Model>();
        model->skeleton = std::make_shared<Skeleton>();
        for (const char* name : {"Parado", "Andando", "Correndo", "Pulando"}) {
            auto clip = std::make_shared<AnimationClip>();
            clip->name = name;
            clip->duration = 1.0f;
            clip->skeleton = model->skeleton;
            model->clips.push_back(clip);
        }
        auto addAnimated = [&](Scene& scene, const glm::vec3& position) {
            Entity& agent = AddAgent(scene, position);
            NavMeshAgentAnimation& anim = agent.Add<NavMeshAgentAnimation>();
            anim.runClip = "Correndo";
            anim.jumpClip = "Pulando";
            const EntityId id = agent.id;
            Entity& body = scene.CreateEntity("Modelo", id);  // o Animator fica no filho, como num glTF
            Animator& animator = body.Add<Animator>();
            animator.model = model;
            animator.clip.clear();
            return std::make_pair(id, body.id);
        };

        Scene scene = MakeScene();
        const auto [id, bodyId] = addAnimated(scene, {-8.0f, 0.0f, 5.0f});
        SceneRuntime runtime(scene);
        runtime.Start();
        NavSystem* nav = runtime.GetSystem<NavSystem>();
        const Animator* animator = scene.Find(bodyId)->Get<Animator>();
        runtime.FixedUpdate(kDt);
        CHECK(animator->CurrentClip() == "Parado" && Near(animator->speed, 1.0f, 1e-4f));

        // Acelera até 3,5 m/s: anda (até ~3 m/s) e corre; o Animator acompanha (3,5 / 4 na corrida).
        CHECK(nav && nav->SetDestination(id, {8.0f, 0.0f, 5.0f}));
        bool walked = false, ran = false, ranAtPace = false;
        for (int i = 0; i < 60 * 10 && nav && !nav->HasArrived(id); ++i) {
            runtime.FixedUpdate(kDt);
            runtime.Update(kDt);
            const float speed = glm::length(nav->GetVelocity(id));
            if (animator->CurrentClip() == "Andando") walked = true;
            if (animator->CurrentClip() == "Correndo") {
                ran = true;
                if (speed > 3.4f && Near(animator->speed, speed / 4.0f, 0.01f)) ranAtPace = true;
            }
        }
        CHECK(walked && ran && ranAtPace);
        for (int i = 0; i < 60; ++i) runtime.FixedUpdate(kDt);  // chegou e freou
        CHECK(animator->CurrentClip() == "Parado" && Near(animator->speed, 1.0f, 1e-4f));

        // Mais devagar que o meio entre andar e correr: só anda, no ritmo (2 / 1,5).
        scene.Find(id)->Get<NavMeshAgent>()->speed = 2.0f;
        CHECK(nav && nav->SetDestination(id, {-8.0f, 0.0f, 5.0f}));
        bool ranSlow = false, walkedAtPace = false;
        for (int i = 0; i < 60 * 10 && nav && !nav->HasArrived(id); ++i) {
            runtime.FixedUpdate(kDt);
            const float speed = glm::length(nav->GetVelocity(id));
            if (animator->CurrentClip() == "Correndo") ranSlow = true;
            if (speed > 1.95f && animator->CurrentClip() == "Andando" && Near(animator->speed, speed / 1.5f, 0.01f)) {
                walkedAtPace = true;
            }
        }
        CHECK(!ranSlow && walkedAtPace);
        runtime.Stop();

        // Na ligação, o clipe de pular.
        Scene jump;
        Entity& a = jump.CreateEntity("A");
        a.transform.position = {-5.0f, -0.5f, 0.0f};
        a.Add<ColliderComponent>().size = {8.0f, 1.0f, 8.0f};
        Entity& b = jump.CreateEntity("B");
        b.transform.position = {5.0f, -0.5f, 0.0f};
        b.Add<ColliderComponent>().size = {8.0f, 1.0f, 8.0f};
        jump.CreateEntity("Navegação").Add<NavMeshSurface>();
        NavMeshLink& link = jump.CreateEntity("Pulo").Add<NavMeshLink>();
        link.start = {-1.6f, 0.0f, 0.0f};
        link.end = {1.6f, 0.0f, 0.0f};
        const auto [jumper, jumperBody] = addAnimated(jump, {-6.0f, 0.0f, 0.0f});
        SceneRuntime jumpRuntime(jump);
        jumpRuntime.Start();
        NavSystem* nav2 = jumpRuntime.GetSystem<NavSystem>();
        const Animator* jumperAnimator = jump.Find(jumperBody)->Get<Animator>();
        CHECK(nav2 && nav2->SetDestination(jumper, {6.0f, 0.0f, 0.0f}));
        bool jumpedClip = true, sawLink = false;
        for (int i = 0; i < 60 * 10 && nav2 && !nav2->HasArrived(jumper); ++i) {
            jumpRuntime.FixedUpdate(kDt);
            if (nav2->IsOnLink(jumper)) {
                sawLink = true;
                if (jumperAnimator->CurrentClip() != "Pulando") jumpedClip = false;
            }
        }
        CHECK(sawLink && jumpedClip);
        CHECK(nav2 && nav2->HasArrived(jumper) && jumperAnimator->CurrentClip() != "Pulando");
        jumpRuntime.Stop();
    }

    SECTION("Arquivo .mnav: grava, lê e as consultas dão o mesmo resultado");
    {
        NavGeometry g;
        AddFloor(g, 20.0f);
        AddBox(g, {-6.0f, 0.0f, -0.5f}, {6.0f, 3.0f, 0.5f});
        NavMeshBuildSettings settings;
        settings.agentRadius = 0.4f;
        auto built = NavMesh::Build(g, settings);
        CHECK(built != nullptr);
        const fs::path file = base / PathFromUtf8("Navegação ç") / "Parede.mnav";
        fs::create_directories(file.parent_path());
        std::string error;
        CHECK(built && built->Save(file, &error));
        auto loaded = NavMesh::Load(file, &error);
        CHECK(loaded != nullptr);
        if (built && loaded) {
            CHECK(loaded->PolygonCount() == built->PolygonCount());
            CHECK(loaded->Source() == built->Source());
            CHECK(loaded->Source() == NavMesh::SourceHash(g, settings));
            CHECK(Near(loaded->Settings().agentRadius, 0.4f, 1e-6f));
            std::vector<glm::vec3> a, b;
            CHECK(built->FindPath({0, 0, -5}, {0, 0, 5}, a) && loaded->FindPath({0, 0, -5}, {0, 0, 5}, b));
            CHECK(a.size() == b.size() && Near(NavMesh::PathLength(a), NavMesh::PathLength(b), 1e-4f));
        }

        // Outra geometria ou outro parâmetro: outra impressão digital.
        NavMeshBuildSettings other = settings;
        other.agentRadius = 0.5f;
        CHECK(NavMesh::SourceHash(g, other) != NavMesh::SourceHash(g, settings));
        NavGeometry moved = g;
        moved.vertices.back().x += 0.01f;
        CHECK(NavMesh::SourceHash(moved, settings) != NavMesh::SourceHash(g, settings));

        // Arquivos que não servem: não existe, não é .mnav, versão mais nova, cortado.
        auto write = [&](const char* name, const std::string& bytes) {
            std::ofstream(base / name, std::ios::binary) << bytes;
            return base / name;
        };
        error.clear();
        CHECK(NavMesh::Load(base / "nao-existe.mnav", &error) == nullptr && !error.empty());
        error.clear();
        CHECK(NavMesh::Load(write("texto.mnav", "isto nao e um navmesh"), &error) == nullptr && !error.empty());
        std::string newer = "MNAV";
        newer += static_cast<char>(99);  // versão 99, little-endian
        newer.append(3, static_cast<char>(0));
        error.clear();
        CHECK(NavMesh::Load(write("nova.mnav", newer), &error) == nullptr);
        CHECK(error.find("versão 99") != std::string::npos);
        std::ifstream in(file, std::ios::binary);
        const std::string whole((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        error.clear();
        CHECK(NavMesh::Load(write("cortado.mnav", whole.substr(0, whole.size() / 2)), &error) == nullptr && !error.empty());
    }

    SECTION("Navmesh do projeto: GUID, cena salva, Play usa o arquivo e avisa quando fica velho");
    {
        std::string error;
        auto project = Project::Create(base, "Jogo Ação", &error);
        CHECK(project.has_value());
        if (project) {
            AssetLibrary assets;
            assets.SetRoot(project->RootDir());
            const fs::path assetsDir = project->AssetsDir();
            CHECK(AssetTypeFromPath("x.MNAV") == AssetType::NavMesh);
            CHECK(std::string(AssetTypeName(AssetType::NavMesh)) == "Navmesh");

            // O que o botão "Gerar" do Inspector faz: gera da cena e grava no projeto.
            Scene scene = MakeScene();
            NavMeshSurface& surface = *scene.FindByName("Navegação")->Get<NavMeshSurface>();
            NavStats stats;
            auto nav = BuildSceneNavMesh(scene, surface, &stats);
            CHECK(nav != nullptr && stats.polygons > 0 && stats.triangles == 24);
            const fs::path file = assetsDir / "Navegacao" / "Navmesh.mnav";
            fs::create_directories(file.parent_path());
            CHECK(nav && nav->Save(file, &error));
            surface.navMesh.file = file;
            surface.navMesh.guid = assets.Database().GuidFor(file);
            surface.navMesh.path = assets.KeyForFile(file);
            CHECK(!surface.navMesh.guid.empty());
            CHECK(surface.navMesh.path == "Assets/Navegacao/Navmesh.mnav");
            CHECK(fs::exists(fs::path(file).concat(".meta")));

            // A cena guarda GUID e caminho; ao ler, o arquivo vem do GUID (mesmo depois de mover o .mnav).
            const fs::path sceneFile = assetsDir / "Inicio.mscene";
            CHECK(SceneSerializer::Save(sceneFile, scene, assets, &error));
            const fs::path movedFile = assetsDir / "Outra" / "Navmesh.mnav";
            fs::create_directories(movedFile.parent_path());
            fs::rename(file, movedFile);
            fs::rename(fs::path(file).concat(".meta"), fs::path(movedFile).concat(".meta"));
            assets.Database().Refresh();
            Scene loaded;
            CHECK(SceneSerializer::Load(sceneFile, loaded, assets, &error));
            const Entity* loadedSurface = loaded.FindByName("Navegação");
            const NavMeshData data = loadedSurface ? loadedSurface->Get<NavMeshSurface>()->navMesh : NavMeshData{};
            CHECK(data.guid == surface.navMesh.guid);
            CHECK(data.path == "Assets/Outra/Navmesh.mnav");
            CHECK(!data.file.empty() && fs::equivalent(data.file, movedFile));

            // No Play o navmesh vem do arquivo, e não está velho.
            {
                const EntityId agent = AddAgent(loaded, {0.0f, 0.0f, -5.0f}).id;
                SceneRuntime runtime(loaded);
                runtime.Start();
                NavSystem* system = runtime.GetSystem<NavSystem>();
                CHECK(system && system->GetNavMesh() && system->Stats().fromFile && !system->Stats().stale);
                CHECK(system && system->SetDestination(agent, {0.0f, 0.0f, 5.0f}));
                for (int i = 0; i < 60 * 10 && system && !system->HasArrived(agent); ++i) runtime.FixedUpdate(kDt);
                CHECK(system && system->HasArrived(agent));
                runtime.Stop();
            }

            // A parede mudou de lugar depois de gerar: continua usando o arquivo, mas avisa que está velho.
            loaded.FindByName("Parede")->transform.position.x = 3.0f;
            {
                SceneRuntime runtime(loaded);
                runtime.Start();
                NavSystem* system = runtime.GetSystem<NavSystem>();
                CHECK(system && system->Stats().fromFile && system->Stats().stale);
                // Rebuild gera da cena de agora (sem o arquivo).
                CHECK(system && system->Rebuild() && !system->Stats().fromFile);
                runtime.Stop();
            }

            // Arquivo ligado que sumiu: gera no Play, como sem arquivo.
            fs::remove(movedFile);
            {
                SceneRuntime runtime(loaded);
                runtime.Start();
                NavSystem* system = runtime.GetSystem<NavSystem>();
                CHECK(system && system->GetNavMesh() && !system->Stats().fromFile);
                runtime.Stop();
            }
        }
    }

    return TestResult();
}
