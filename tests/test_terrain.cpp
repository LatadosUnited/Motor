// Fase 28: terreno com mapa de altura em blocos (TerrainData) e o componente Terrain.

#include "TestUtil.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/assets/AssetLoaders.h"
#include "motor/assets/TerrainData.h"
#include "motor/common/FileRead.h"
#include "motor/project/Project.h"
#include "motor/renderer/Mesh.h"
#include "motor/scene/Components.h"
#include "motor/scene/Scene.h"
#include "motor/scene/SceneSerializer.h"

#include <glm/geometric.hpp>

#include <cmath>
#include <fstream>

using namespace motor;
namespace fs = std::filesystem;

namespace {
bool Near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) <= eps; }

// Morro suave: dá alturas diferentes em toda amostra.
float Hill(int gx, int gz) { return 3.0f * std::sin(static_cast<float>(gx) * 0.1f) + 0.05f * static_cast<float>(gz); }

TerrainData MakeHills(int resolution = 9) {
    TerrainData data(8.0f, resolution);
    for (int z = -1; z <= 0; ++z) {
        for (int x = -1; x <= 1; ++x) data.AddBlock({x, z});
    }
    const int n = resolution - 1;
    for (int gz = -n; gz <= n; ++gz) {
        for (int gx = -n; gx <= 2 * n; ++gx) data.SetSample(gx, gz, Hill(gx, gz));
    }
    return data;
}
} // namespace

int main(int argc, char** argv) {
    const fs::path base = TestDir(argc, argv, "test_terrain_out");

    SECTION("Amostras: a borda é dos dois blocos, fora dos blocos não há altura");
    {
        TerrainData data = MakeHills();
        CHECK(data.BlockCount() == 6 && data.Spacing() == 1.0f);
        // gx = 8 é a última coluna do bloco 0 e a primeira do bloco 1: as duas cópias mudaram juntas.
        CHECK(Near((*data.BlockHeights({0, 0}))[0 * 9 + 8], Hill(8, 0)));
        CHECK(Near((*data.BlockHeights({1, 0}))[0 * 9 + 0], Hill(8, 0)));
        // Canto de quatro blocos (0, 0): os quatro guardam o mesmo valor.
        CHECK(Near((*data.BlockHeights({-1, -1}))[8 * 9 + 8], Hill(0, 0)));
        CHECK(Near((*data.BlockHeights({0, -1}))[8 * 9 + 0], Hill(0, 0)));
        CHECK(Near((*data.BlockHeights({-1, 0}))[0 * 9 + 8], Hill(0, 0)));
        CHECK(Near(*data.Sample(-3, -5), Hill(-3, -5)));
        CHECK(!data.Sample(0, 9).has_value() && !data.Sample(-9, 0).has_value() && !data.Sample(25, 0).has_value());
        // Ordem de Blocks(): z, depois x.
        const auto keys = data.Blocks();
        CHECK(keys.front() == (TerrainBlockKey{-1, -1}) && keys[1] == (TerrainBlockKey{0, -1}) &&
              keys.back() == (TerrainBlockKey{1, 0}));
        CHECK(!data.AddBlock({0, 0}));
    }

    SECTION("Altura num ponto: interpolada nos triângulos da malha");
    {
        TerrainData data = MakeHills();
        CHECK(Near(*data.HeightAt(3.0f, 2.0f), Hill(3, 2)));
        // No meio da aresta entre duas amostras: a média.
        CHECK(Near(*data.HeightAt(3.5f, 2.0f), 0.5f * (Hill(3, 2) + Hill(4, 2))));
        // Na diagonal do quadrado (mesma nos dois triângulos): a média das duas pontas dela.
        CHECK(Near(*data.HeightAt(3.5f, 2.5f), 0.5f * (Hill(4, 2) + Hill(3, 3))));
        CHECK(!data.HeightAt(0.0f, 8.5f).has_value());
        const auto [lo, hi] = data.BlockHeightRange({0, 0});
        CHECK(lo < hi && lo >= -3.1f && hi <= 3.5f);
    }

    SECTION("Malha do bloco: vértices do canto, normais sem emenda, refeita só quando muda");
    {
        TerrainData flat(8.0f, 9);
        flat.AddBlock({0, 0});
        auto mesh = flat.BlockMesh({0, 0});
        CHECK(mesh && mesh->Positions().size() == 81 && mesh->Indices().size() == 8 * 8 * 6);
        CHECK(Near(mesh->BoundsMax().x, 8.0f) && Near(mesh->BoundsMin().x, 0.0f));
        // Triângulos virados para cima (+Y), como o chão.
        const auto& p = mesh->Positions();
        const auto& idx = mesh->Indices();
        const glm::vec3 n = glm::cross(p[idx[1]] - p[idx[0]], p[idx[2]] - p[idx[0]]);
        CHECK(n.y > 0.0f);
        CHECK(flat.BlockMesh({0, 0}) == mesh);  // nada mudou: a mesma
        // Rampa subindo em x (meio metro por metro): a normal deita para -x, também na borda do terreno.
        TerrainData ramp(8.0f, 9);
        ramp.AddBlock({0, 0});
        for (int gz = 0; gz <= 8; ++gz) {
            for (int gx = 0; gx <= 8; ++gx) ramp.SetSample(gx, gz, 0.5f * static_cast<float>(gx));
        }
        const glm::vec3 slope = glm::normalize(glm::vec3(-0.5f, 1.0f, 0.0f));
        CHECK(glm::dot(ramp.NormalAt(4, 4), slope) > 0.9999f);
        CHECK(glm::dot(ramp.NormalAt(0, 0), slope) > 0.9f && ramp.NormalAt(0, 0).x < 0.0f);
        CHECK(Near(ramp.BlockMesh({0, 0})->BoundsMax().y, 4.0f));
        CHECK(flat.BlockMesh({5, 5}) == nullptr);

        TerrainData data = MakeHills();
        auto left = data.BlockMesh({0, 0}), right = data.BlockMesh({1, 0});
        // A coluna da borda é a mesma amostra nos dois blocos: altura igual (em mundo) e normal igual.
        for (int j = 0; j < 9; ++j) {
            const glm::vec3 a = left->Positions()[static_cast<size_t>(j * 9 + 8)];
            const glm::vec3 b = right->Positions()[static_cast<size_t>(j * 9 + 0)];
            CHECK(Near(a.y, b.y) && Near(a.x, 8.0f) && Near(b.x, 0.0f));
        }
        // Mudar uma amostra perto da borda refaz os dois (a normal do vizinho também usa ela).
        data.SetSample(9, 3, 10.0f);
        CHECK(data.BlockMesh({0, 0}) != left && data.BlockMesh({1, 0}) != right);
        auto far = data.BlockMesh({-1, -1});
        data.SetSample(12, 3, 5.0f);
        CHECK(data.BlockMesh({-1, -1}) == far);  // longe: fica como estava
    }

    SECTION("Arquivos: cabeçalho e um arquivo por bloco; gravar de novo só os mudados");
    {
        const fs::path file = base / "Ilha Ação.mterrain";
        TerrainData data = MakeHills();
        std::string error;
        CHECK(data.IsDirty());
        CHECK(data.Save(file, &error));
        CHECK(!data.IsDirty());
        const fs::path folder = base / "Ilha Ação.blocos";
        CHECK(TerrainData::BlocksFolder(file) == folder);
        CHECK(fs::is_regular_file(folder / "-1_-1.mtbloco") && fs::is_regular_file(folder / "1_0.mtbloco"));
        CHECK(fs::file_size(folder / "0_0.mtbloco") == 20 + 81 * sizeof(float));
        CHECK(AssetTypeFromPath(file) == AssetType::Terrain);
        CHECK(std::string(AssetTypeName(AssetType::Terrain)) == "Terreno");
        const auto companions = AssetCompanions(file);
        CHECK(companions.size() == 1 && companions.front() == folder);
        CHECK(AssetCompanions(base / "Outro.mterrain").empty());

        auto loaded = TerrainData::Load(file, &error);
        CHECK(loaded && loaded->BlockCount() == 6 && loaded->Resolution() == 9 && loaded->BlockSize() == 8.0f);
        CHECK(loaded && !loaded->IsDirty());
        CHECK(loaded && *loaded->BlockHeights({1, 0}) == *data.BlockHeights({1, 0}));

        // Só o bloco mudado é regravado (e o que sumiu do disco): os outros ficam com a data de antes.
        const auto stamp = fs::last_write_time(folder / "-1_-1.mtbloco");
        const auto stampChanged = fs::last_write_time(folder / "0_0.mtbloco");
        fs::remove(folder / "1_0.mtbloco");
        loaded->SetSample(3, 3, 7.0f);  // só no bloco (0, 0)
        CHECK(loaded->Save(file, &error));
        CHECK(fs::last_write_time(folder / "-1_-1.mtbloco") == stamp);
        CHECK(fs::last_write_time(folder / "0_0.mtbloco") >= stampChanged);
        CHECK(fs::exists(folder / "1_0.mtbloco"));
        CHECK(Near(*TerrainData::Load(file)->Sample(3, 3), 7.0f));
        fs::remove(folder / "1_0.mtbloco");

        // Bloco que falta vira plano, com aviso; gravar em outro arquivo grava todos.
        error.clear();
        auto partial = TerrainData::Load(file, &error);
        CHECK(partial && partial->BlockCount() == 6 && !error.empty());
        CHECK(partial && Near((*partial->BlockHeights({1, 0}))[40], 0.0f) && partial->IsDirty());
        const fs::path copy = base / "Copia.mterrain";
        CHECK(partial->Save(copy, &error));
        CHECK(fs::is_regular_file(base / "Copia.blocos" / "-1_-1.mtbloco"));

        // Bloco estragado: plano, o resto lido.
        {
            std::ofstream bad(folder / "0_-1.mtbloco", std::ios::binary | std::ios::trunc);
            bad << "lixo";
        }
        error.clear();
        auto broken = TerrainData::Load(file, &error);
        CHECK(broken && error.find("0_-1.mtbloco") != std::string::npos);
        CHECK(broken && Near(*broken->Sample(3, 3), 7.0f));

        // Cabeçalho que não é terreno, ou de versão mais nova: nada.
        {
            std::ofstream other(base / "Nao.mterrain", std::ios::trunc);
            other << R"({"format": "motor-scene"})";
            std::ofstream newer(base / "Nova.mterrain", std::ios::trunc);
            newer << R"({"format": "motor-terrain", "version": 99})";
        }
        CHECK(TerrainData::Load(base / "Nao.mterrain", &error) == nullptr);
        CHECK(TerrainData::Load(base / "Nova.mterrain", &error) == nullptr);
        CHECK(TerrainData::Load(base / "Nenhum.mterrain", &error) == nullptr);

        // Open divide o objeto enquanto alguém o usa.
        auto a = TerrainData::Open(copy), b = TerrainData::Open(copy);
        CHECK(a && a == b);
    }

    SECTION("Componente: cena salva e lida, blocos desenhados como estáticos, raio acerta o terreno");
    {
        std::string error;
        auto project = Project::Create(base, "Jogo Ação", &error);
        CHECK(project.has_value());
        if (project) {
            AssetLibrary assets;
            assets.SetRoot(project->RootDir());
            const fs::path file = project->AssetsDir() / "Terrenos" / "Vale.mterrain";
            fs::create_directories(file.parent_path());
            TerrainData data(32.0f, 33);
            for (int z = -2; z < 2; ++z) {
                for (int x = -2; x < 2; ++x) data.AddBlock({x, z}, 1.5f);
            }
            CHECK(data.Save(file, &error));

            Scene scene;
            Entity& e = scene.CreateEntity("Terreno");
            e.transform.position = {10.0f, 2.0f, 0.0f};
            Terrain& terrain = e.Add<Terrain>();
            terrain.terrain.file = file;
            terrain.terrain.guid = assets.Database().GuidFor(file);
            terrain.terrain.path = assets.KeyForFile(file);
            CHECK(terrain.terrain.path == "Assets/Terrenos/Vale.mterrain");
            CHECK(terrain.terrain.Data() && terrain.terrain.Data()->BlockCount() == 16);
            CHECK(scene.StaticRenderCount() == 0);
            // Raio de cima: acerta a 10 - (2 + 1.5) = 6,5 m.
            const auto hit = scene.Raycast({0.0f, 10.0f, 5.0f}, {0.0f, -1.0f, 0.0f});
            CHECK(hit && hit->entity == e.id && Near(hit->distance, 6.5f, 1e-3f));
            CHECK(scene.StaticRenderCount() == 16);
            // Fora dos 128 m: nada.
            CHECK(!scene.Raycast({80.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}).has_value());
            // Altura mudada: o raio vê a nova (os estáticos são refeitos sozinhos).
            terrain.terrain.Data()->SetSample(-10, 5, 4.0f);
            const auto raised = scene.Raycast({0.0f, 10.0f, 5.0f}, {0.0f, -1.0f, 0.0f});
            CHECK(raised && Near(raised->distance, 4.0f, 1e-3f));

            const fs::path sceneFile = project->AssetsDir() / "Vale.mscene";
            CHECK(SceneSerializer::Save(sceneFile, scene, assets, &error));
            CHECK(ReadTextFile(sceneFile).value_or("").find("Assets/Terrenos/Vale.mterrain") != std::string::npos);
            Scene loaded;
            CHECK(SceneSerializer::Load(sceneFile, loaded, assets, &error));
            const Entity* le = loaded.FindByName("Terreno");
            const Terrain* lt = le ? le->Get<Terrain>() : nullptr;
            CHECK(lt && lt->terrain == terrain.terrain && !lt->terrain.file.empty());
            CHECK(lt && Near(lt->material.color.g, Terrain::DefaultMaterial().color.g));
            // A mesma TerrainData (Open): a altura mudada (e não salva) aparece na cena lida.
            CHECK(lt && lt->terrain.Data() == terrain.terrain.Data());
            CHECK(loaded == scene);

            // Cópia da cena (desfazer, Play): divide as alturas.
            Scene copy(scene);
            CHECK(copy.Find(e.id)->Get<Terrain>()->terrain.Data() == terrain.terrain.Data());

            CHECK(ComponentRegistry::TypeOf<Terrain>() != nullptr);
        }
    }
    return TestResult();
}
