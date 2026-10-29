// Fase 28: grade de células (SpatialGrid). As consultas têm de dar o mesmo que testar todos os objetos.

#include "TestUtil.h"

#include "motor/renderer/SpatialGrid.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace motor;

namespace {

bool Touch(const Aabb& a, const Aabb& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

std::vector<uint32_t> Sorted(std::vector<uint32_t> ids) {
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool NoRepeats(std::vector<uint32_t> ids) {
    ids = Sorted(std::move(ids));
    return std::adjacent_find(ids.begin(), ids.end()) == ids.end();
}

Aabb Box(glm::vec3 center, glm::vec3 half) { return {center - half, center + half}; }

Frustum CameraFrustum(glm::vec3 eye, glm::vec3 target, float farPlane) {
    const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, farPlane);
    return Frustum::FromMatrix(projection * glm::lookAt(eye, target, {0.0f, 1.0f, 0.0f}));
}

} // namespace

int main() {
    SECTION("Células: coordenadas negativas arredondam para baixo");
    {
        SpatialGrid grid(10.0f);
        CHECK((grid.CellOf({5.0f, 0.0f, 5.0f}) == GridCell{0, 0}));
        CHECK((grid.CellOf({-0.5f, 0.0f, -10.0f}) == GridCell{-1, -1}));
        CHECK((grid.CellOf({-10.5f, 99.0f, 25.0f}) == GridCell{-2, 2}));
        const Aabb bounds = grid.CellBounds({-1, 2}, 0.0f, 1.0f);
        CHECK(bounds.min.x == -10.0f && bounds.max.x == 0.0f && bounds.min.z == 20.0f && bounds.max.z == 30.0f);
    }

    // Mundo de 2 x 2 km com objetos pequenos, médios (várias células) e alguns gigantes (lista à parte).
    std::mt19937 rng(28);
    std::uniform_real_distribution<float> pos(-1000.0f, 1000.0f), height(0.0f, 60.0f), size(0.2f, 6.0f);
    SpatialGrid grid(32.0f);
    std::vector<Aabb> boxes;
    std::vector<uint32_t> ids;
    auto add = [&](const Aabb& box) {
        ids.push_back(grid.Insert(box));
        boxes.push_back(box);
    };
    for (int i = 0; i < 4000; ++i) add(Box({pos(rng), height(rng), pos(rng)}, glm::vec3(size(rng))));
    for (int i = 0; i < 40; ++i) add(Box({pos(rng), 10.0f, pos(rng)}, {50.0f, 10.0f, 40.0f}));
    add(Box({0.0f, -1.0f, 0.0f}, {1000.0f, 1.0f, 1000.0f}));  // chão do mundo inteiro

    auto bruteFrustum = [&](const Frustum& frustum) {
        std::vector<uint32_t> out;
        for (size_t i = 0; i < ids.size(); ++i) {
            if (grid.Contains(ids[i]) && frustum.Intersects(boxes[i])) out.push_back(ids[i]);
        }
        return Sorted(out);
    };
    auto bruteBox = [&](const Aabb& query) {
        std::vector<uint32_t> out;
        for (size_t i = 0; i < ids.size(); ++i) {
            if (grid.Contains(ids[i]) && Touch(boxes[i], query)) out.push_back(ids[i]);
        }
        return Sorted(out);
    };

    SECTION("Frustum: o mesmo que testar todos, sem repetir");
    {
        CHECK(grid.Count() == ids.size());
        bool same = true, unique = true;
        size_t total = 0;
        const glm::vec3 eyes[] = {{0, 20, 0}, {-900, 5, -900}, {500, 200, -300}, {1200, 30, 0}};
        const glm::vec3 targets[] = {{100, 0, 300}, {0, 0, 0}, {500, 0, -300.5f}, {-1000, 0, 0}};
        for (int i = 0; i < 4; ++i) {
            const Frustum frustum = CameraFrustum(eyes[i], targets[i], 400.0f);
            std::vector<uint32_t> found;
            grid.QueryFrustum(frustum, found);
            unique = unique && NoRepeats(found);
            same = same && Sorted(found) == bruteFrustum(frustum);
            total += found.size();
        }
        CHECK(same);
        CHECK(unique);
        // A câmera vê uma parte pequena do mundo: bem menos que todos (o chão e alguns grandes sempre entram).
        CHECK(total > 0 && total < ids.size());
    }

    SECTION("Caixa: área pequena, área maior que o mundo e caixa sem nada");
    {
        const Aabb queries[] = {Box({10, 10, 10}, {40, 40, 40}), Box({0, 0, 0}, {5000, 5000, 5000}),
                                Box({-333, 30, 777}, {3, 1, 3}), Box({0, 500, 0}, {1, 1, 1})};
        bool same = true, unique = true;
        for (const Aabb& query : queries) {
            std::vector<uint32_t> found;
            grid.QueryBox(query, found);
            unique = unique && NoRepeats(found);
            same = same && Sorted(found) == bruteBox(query);
        }
        CHECK(same);
        CHECK(unique);
        std::vector<uint32_t> all;
        grid.QueryBox(Box({0, 0, 0}, {5000, 5000, 5000}), all);
        CHECK(all.size() == ids.size());
    }

    SECTION("Mover e remover");
    {
        // Metade dos pequenos anda (uns pouco, dentro da célula; outros para longe) e um quarto sai.
        for (size_t i = 0; i < 2000; ++i) {
            const glm::vec3 shift = i % 2 ? glm::vec3(0.01f, 0.0f, 0.0f) : glm::vec3(pos(rng), 0.0f, pos(rng));
            boxes[i] = {boxes[i].min + shift, boxes[i].max + shift};
            grid.Update(ids[i], boxes[i]);
        }
        for (size_t i = 2000; i < 3000; ++i) grid.Remove(ids[i]);
        CHECK(grid.Count() == ids.size() - 1000);
        grid.Update(ids[2500], Box({}, glm::vec3(1.0f)));  // removido: ignorado
        CHECK(!grid.Contains(ids[2500]));

        const Frustum frustum = CameraFrustum({0, 20, 0}, {100, 0, 300}, 600.0f);
        std::vector<uint32_t> found;
        grid.QueryFrustum(frustum, found);
        CHECK(Sorted(found) == bruteFrustum(frustum));
        CHECK(NoRepeats(found));
        const Aabb area = Box({-200, 20, 150}, {120, 40, 120});
        found.clear();
        grid.QueryBox(area, found);
        CHECK(Sorted(found) == bruteBox(area));

        // Identificadores removidos voltam a ser usados.
        const uint32_t reused = grid.Insert(Box({7, 7, 7}, glm::vec3(1.0f)));
        CHECK(reused >= 2000 && reused < 3000);
        CHECK(grid.Count() == ids.size() - 999);
        grid.Remove(reused);
    }

    SECTION("Altura da célula acompanha quem sai e quem sobe");
    {
        SpatialGrid small(10.0f);
        const uint32_t low = small.Insert(Box({5, 1, 5}, glm::vec3(1.0f)));
        const uint32_t tall = small.Insert(Box({5, 100, 5}, glm::vec3(1.0f)));
        small.Remove(tall);
        // Frustum olhando de cima, só para y entre 50 e 150: a célula não pode mais parecer alta.
        const Frustum high = CameraFrustum({5, 200, 5.01f}, {5, 0, 5}, 150.0f);
        std::vector<uint32_t> found;
        small.QueryFrustum(high, found);
        CHECK(found.empty());
        small.Update(low, Box({5, 100, 5}, glm::vec3(1.0f)));  // mesmo lugar no chão, só sobe
        small.QueryFrustum(high, found);
        CHECK(found.size() == 1 && found[0] == low);
        small.Clear();
        CHECK(small.Count() == 0 && small.CellCount() == 0);
    }

    SECTION("Raio: do mais perto ao mais longe, com eixos parados");
    {
        SpatialGrid rays(4.0f);
        const uint32_t near = rays.Insert(Box({10, 0, 0}, glm::vec3(1.0f)));
        const uint32_t far = rays.Insert(Box({30, 0, 0}, glm::vec3(1.0f)));
        const uint32_t beside = rays.Insert(Box({20, 5, 0}, glm::vec3(1.0f)));
        const uint32_t wide = rays.Insert(Box({0, -1, 0}, {500, 0.5f, 500}));  // gigante, lista à parte
        std::vector<uint32_t> found;
        rays.QueryRay({0, 0, 0}, {1, 0, 0}, 100.0f, found);
        CHECK((found == std::vector<uint32_t>{near, far}));
        found.clear();
        rays.QueryRay({0, 0, 0}, {1, 0, 0}, 20.0f, found);  // o longe passa do alcance
        CHECK((found == std::vector<uint32_t>{near}));
        found.clear();
        rays.QueryRay({20, 20, 0}, {0, -1, 0}, 100.0f, found);  // de cima para baixo: o do lado e o chão
        CHECK((found == std::vector<uint32_t>{beside, wide}));
        found.clear();
        rays.QueryRay({-50, 0, 0}, {-1, 0, 0}, 100.0f, found);  // para longe de tudo
        CHECK(found.empty());
        found.clear();
        rays.QueryRay({30, 0, 0}, {0, 0, 1}, 10.0f, found);  // começa dentro de uma caixa
        CHECK((found == std::vector<uint32_t>{far}));
    }

    return TestResult();
}
