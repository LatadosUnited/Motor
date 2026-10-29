// Fase 28, Tarefa 13: água. Ondas (a conta da CPU, igual à do shader), a grade e o componente na cena.

#include "TestUtil.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/renderer/Mesh.h"
#include "motor/renderer/Water.h"
#include "motor/scene/Components.h"
#include "motor/scene/Scene.h"
#include "motor/scene/SceneSerializer.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace motor;

namespace {
bool Near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) <= eps; }
} // namespace

int main() {
    SECTION("Ondas: altura até a configurada, média zero, andam com o tempo, paradas sem altura");
    {
        WaterSurface w;
        w.waveHeight = 0.5f;
        w.waveLength = 10.0f;
        float peak = 0.0f, side = 0.0f;
        for (int i = 0; i < 4000; ++i) {
            const glm::vec2 p(static_cast<float>(i % 63) * 1.37f - 40.0f, static_cast<float>(i / 63) * 1.91f - 60.0f);
            const glm::vec3 o = w.WaveOffset(p, static_cast<float>(i % 7) * 0.9f);
            peak = std::max(peak, std::abs(o.y));
            side = std::max(side, std::abs(o.x) + std::abs(o.z));
        }
        std::printf("  maior altura %.3f m, maior passo para o lado %.3f m\n", peak, side);
        CHECK(peak <= w.waveHeight + 1e-4f && peak > 0.3f);
        // Média no tempo, em pontos fixos: zero (a água sobe tanto quanto desce).
        float worstMean = 0.0f;
        for (const glm::vec2 p : {glm::vec2(0.0f), glm::vec2(7.3f, -2.1f), glm::vec2(-31.0f, 18.5f)}) {
            float mean = 0.0f;
            for (int t = 0; t < 20000; ++t) mean += w.WaveOffset(p, static_cast<float>(t) * 0.01f).y;
            worstMean = std::max(worstMean, std::abs(mean / 20000.0f));
        }
        std::printf("  média no tempo: %.4f m\n", worstMean);
        CHECK(worstMean < 0.01f);
        CHECK(side > 0.0f && side < 1.0f);  // Gerstner: os pontos andam um pouco para os lados
        // O tempo move a superfície; velocidade 0 a deixa parada.
        CHECK(!Near(w.WaveOffset({3.0f, 4.0f}, 0.0f).y, w.WaveOffset({3.0f, 4.0f}, 0.7f).y, 1e-3f));
        WaterSurface still = w;
        still.waveSpeed = 0.0f;
        CHECK(Near(still.WaveOffset({3.0f, 4.0f}, 0.0f).y, still.WaveOffset({3.0f, 4.0f}, 50.0f).y));
        WaterSurface flat = w;
        flat.waveHeight = 0.0f;
        CHECK(flat.WaveOffset({3.0f, 4.0f}, 2.0f) == glm::vec3(0.0f));
        // Direção: com 0° a onda maior anda em +X. Um passo de meio comprimento em Z muda menos que em X.
        WaterSurface one = w;
        one.waveDirection = 90.0f;
        const float alongZ = std::abs(one.WaveOffset({0.0f, 2.5f}, 0.0f).y - one.WaveOffset({0.0f, 0.0f}, 0.0f).y);
        const float alongX = std::abs(one.WaveOffset({2.5f, 0.0f}, 0.0f).y - one.WaveOffset({0.0f, 0.0f}, 0.0f).y);
        CHECK(alongZ > alongX);
    }

    SECTION("Grade: tamanho, células de até 1 m (no máximo 256 por lado), virada para cima");
    {
        auto grid = CreateWaterGrid({40.0f, 10.0f});
        CHECK(grid && grid->Positions().size() == 41u * 11u && grid->Indices().size() == 40u * 10u * 6u);
        CHECK(grid && Near(grid->BoundsMin().x, -20.0f) && Near(grid->BoundsMax().z, 5.0f) && grid->BoundsMax().y == 0.0f);
        bool up = true;
        const auto& p = grid->Positions();
        const auto& idx = grid->Indices();
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            up = up && glm::cross(p[idx[t + 1]] - p[idx[t]], p[idx[t + 2]] - p[idx[t]]).y > 0.0f;
        }
        CHECK(up);
        auto big = CreateWaterGrid({2000.0f, 3.0f});
        CHECK(big && big->Positions().size() == 257u * 4u);
    }

    SECTION("Componente: grade refeita só quando o tamanho muda, campos na cena salva e lida");
    {
        Scene scene;
        Entity& e = scene.CreateEntity("Lago");
        e.transform.position = {0.0f, 2.0f, 0.0f};
        Water& water = e.Add<Water>();
        const Mesh* first = &water.GridMesh();
        CHECK(&water.GridMesh() == first);
        CHECK(Near(first->BoundsMax().x, 50.0f));
        water.size = {30.0f, 20.0f};
        const Mesh& resized = water.GridMesh();
        CHECK(Near(resized.BoundsMax().x, 15.0f) && Near(resized.BoundsMax().z, 10.0f));
        water.surface.waveHeight = 0.8f;
        water.surface.deepColor = {0.1f, 0.2f, 0.3f};
        water.surface.foam = 0.0f;

        AssetLibrary assets;
        const std::string json = SceneSerializer::ToJson(scene, assets);
        CHECK(json.find("\"Water\"") != std::string::npos && json.find("waveHeight") != std::string::npos);
        Scene loaded;
        std::string error;
        CHECK(SceneSerializer::FromJson(json, loaded, assets, &error));
        const Entity* le = loaded.FindByName("Lago");
        const Water* lw = le ? le->Get<Water>() : nullptr;
        CHECK(lw && lw->size == glm::vec2(30.0f, 20.0f) && Near(lw->surface.waveHeight, 0.8f));
        CHECK(lw && Near(lw->surface.deepColor.b, 0.3f) && lw->surface.foam == 0.0f);
        // A cópia da cena (desfazer, Play) fica igual; mudar um campo da água a deixa diferente.
        Scene copy(scene);
        CHECK(copy == scene);
        copy.Find(e.id)->Get<Water>()->surface.reflection = 0.2f;
        CHECK(!(copy == scene));
    }
    return TestResult();
}
