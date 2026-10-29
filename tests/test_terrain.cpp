// Fase 28: terreno com mapa de altura em blocos (TerrainData) e o componente Terrain.

#include "TestUtil.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/assets/AssetLoaders.h"
#include "motor/assets/TerrainBrush.h"
#include "motor/assets/TerrainData.h"
#include "motor/common/FileRead.h"
#include "motor/common/Text.h"
#include "motor/project/Project.h"
#include "motor/renderer/Mesh.h"
#include "motor/renderer/Texture.h"
#include "motor/scene/Components.h"
#include "motor/scene/Scene.h"
#include "motor/scene/SceneSerializer.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <fstream>
#include <map>

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
        CHECK(fs::file_size(folder / "0_0.mtbloco") == 20 + 81 * (sizeof(float) + 4));  // alturas e os 4 pesos
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

            // SaveTerrains grava a altura mudada (só do terreno sujo) e o arquivo relido a tem.
            CHECK(terrain.terrain.Data()->IsDirty());
            CHECK(SaveTerrains(scene, &error));
            CHECK(!terrain.terrain.Data()->IsDirty());
            const auto reread = TerrainData::Load(file, &error);
            CHECK(reread && Near(reread->Sample(-10, 5).value_or(0.0f), 4.0f));
        }
    }

    SECTION("Pincéis: subir e baixar com queda do centro à borda, só dentro do raio e dos blocos");
    {
        auto data = std::make_shared<TerrainData>(32.0f, 33);
        data->AddBlock({0, 0});
        data->AddBlock({1, 0});
        TerrainBrush brush;
        brush.radius = 8.0f;
        brush.strength = 1.0f;
        TerrainStroke raise(data, brush);
        CHECK(raise.Apply(32.0f, 16.0f, 0.5f) > 0);  // na borda entre os dois blocos
        const float center = data->Sample(32, 16).value_or(0.0f);
        CHECK(Near(center, TerrainBrush::kRaiseSpeed * 0.5f));
        const float middle = data->Sample(36, 16).value_or(0.0f);
        CHECK(middle > 0.0f && middle < center);
        CHECK(Near(data->Sample(40, 16).value_or(-1.0f), 0.0f));  // na borda do raio: nada
        CHECK(Near(data->Sample(32, 25).value_or(-1.0f), 0.0f));
        // A borda dividida subiu nos dois blocos (a malha dos dois bate).
        CHECK(Near(data->BlockHeights({0, 0})->at(16 * 33 + 32), center));
        CHECK(Near(data->BlockHeights({1, 0})->at(16 * 33 + 0), center));
        // Fora dos blocos não há o que mudar, e nada quebra.
        TerrainStroke outside(data, brush);
        CHECK(outside.Apply(-50.0f, -50.0f, 0.5f) == 0);
        CHECK(outside.Finish().Empty());
        // Força 0, ou dt 0: nada.
        CHECK(TerrainStroke(data, TerrainBrush{.strength = 0.0f}).Apply(10.0f, 10.0f, 1.0f) == 0);

        // Finish guarda o antes e o depois de cada amostra mudada; desfazer e refazer põem as alturas.
        const TerrainEdit edit = raise.Finish();
        const auto& changes = edit.heights;
        CHECK(edit.weights.empty());
        CHECK(!changes.empty());
        bool allBefore0 = true;
        for (const auto& c : changes) allBefore0 = allBefore0 && c.before == 0.0f && c.after > 0.0f;
        CHECK(allBefore0);
        ApplyTerrainEdit(*data, edit, false);
        CHECK(Near(data->Sample(32, 16).value_or(-1.0f), 0.0f));
        ApplyTerrainEdit(*data, edit, true);
        CHECK(Near(data->Sample(32, 16).value_or(-1.0f), center));

        // Baixar a mesma coisa volta a zero; a pincelada que volta ao início não tem mudança.
        brush.mode = TerrainBrushMode::Lower;
        TerrainStroke lower(data, brush);
        lower.Apply(32.0f, 16.0f, 0.5f);
        CHECK(Near(data->Sample(32, 16).value_or(-1.0f), 0.0f, 1e-3f));
        CHECK(Near(data->Sample(36, 16).value_or(-1.0f), 0.0f, 1e-3f));
        // Várias passadas na mesma amostra: o "antes" é o do começo da pincelada.
        brush.mode = TerrainBrushMode::Raise;
        TerrainStroke twice(data, brush);
        const float start = data->Sample(32, 16).value_or(0.0f);
        twice.Apply(32.0f, 16.0f, 0.1f);
        twice.Apply(32.0f, 16.0f, 0.1f);
        const auto twiceChanges = twice.Finish().heights;
        const auto it = std::find_if(twiceChanges.begin(), twiceChanges.end(),
                                     [](const TerrainSampleChange& c) { return c.gx == 32 && c.gz == 16; });
        CHECK(it != twiceChanges.end() && Near(it->before, start) && Near(it->after, start + TerrainBrush::kRaiseSpeed * 0.2f));
    }

    SECTION("Pincéis: suavizar diminui o degrau, nivelar leva à altura de onde começou");
    {
        auto data = std::make_shared<TerrainData>(32.0f, 33);
        data->AddBlock({0, 0});
        for (int gz = 0; gz <= 32; ++gz) {
            for (int gx = 0; gx <= 32; ++gx) data->SetSample(gx, gz, gx < 16 ? 0.0f : 4.0f);  // degrau em x = 16
        }
        TerrainBrush brush{.mode = TerrainBrushMode::Smooth, .radius = 6.0f, .strength = 1.0f};
        TerrainStroke smooth(data, brush);
        for (int i = 0; i < 20; ++i) smooth.Apply(16.0f, 16.0f, 0.1f);
        const float low = data->Sample(15, 16).value_or(0.0f), high = data->Sample(16, 16).value_or(0.0f);
        CHECK(low > 0.5f && high < 3.5f && low < high);  // o degrau virou rampa, sem inverter
        CHECK(Near(data->Sample(15, 2).value_or(-1.0f), 0.0f));  // longe do pincel: igual

        TerrainStroke flatten(data, TerrainBrush{.mode = TerrainBrushMode::Flatten, .radius = 6.0f, .strength = 1.0f});
        flatten.Apply(4.0f, 4.0f, 1.0f);  // começa no plano baixo (altura 0)...
        for (int i = 0; i < 30; ++i) flatten.Apply(26.0f, 26.0f, 0.2f);  // ...e arrasta para o alto
        CHECK(Near(data->Sample(26, 26).value_or(-1.0f), 0.0f, 1e-2f));
        CHECK(Near(data->Sample(31, 31).value_or(-1.0f), 4.0f));  // fora do raio: igual
    }

    SECTION("Pintura: pesos na borda dividida, soma 1, desfazer, sem refazer a malha");
    {
        auto data = std::make_shared<TerrainData>(32.0f, 33);
        data->AddBlock({0, 0});
        data->AddBlock({1, 0});
        CHECK(data->Weights(5, 5) == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));  // começa todo na camada 1
        CHECK(!data->Weights(-5, 5).has_value());
        const auto mesh = data->BlockMesh({0, 0});
        const uint64_t version = data->Version();

        TerrainStroke paint(data, TerrainBrush{.mode = TerrainBrushMode::Paint, .radius = 6.0f, .strength = 1.0f, .layer = 1});
        for (int i = 0; i < 20; ++i) paint.Apply(32.0f, 16.0f, 0.1f);
        const glm::vec4 center = data->Weights(32, 16).value_or(glm::vec4(0.0f));
        CHECK(center.y > 0.99f && center.x < 0.01f);
        const glm::vec4 side = data->Weights(35, 16).value_or(glm::vec4(0.0f));
        CHECK(side.y > 0.0f && side.x > 0.0f && Near(side.x + side.y + side.z + side.w, 1.0f));
        CHECK(data->Weights(38, 16) == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));  // na borda do raio: nada
        CHECK(data->Version() != version);  // a cena refaz os blocos (material com os pesos novos)
        CHECK(data->BlockMesh({0, 0}) == mesh);  // a malha não muda
        CHECK(data->IsDirty());

        // Os pesos de todos os blocos numa textura só (uma camada por bloco, 33 x 33), o mesmo objeto sempre.
        const auto texture = data->WeightsTexture();
        CHECK(texture && texture->Resolution() == 33 && texture->BlockCount() == 2);
        CHECK(texture->LayerOf(0, 0) >= 0 && texture->LayerOf(1, 0) >= 0 && texture->LayerOf(0, 0) != texture->LayerOf(1, 0));
        CHECK(data->WeightsTexture() == texture);

        // SetWeights normaliza; tudo zero vira a camada 1.
        data->SetWeights(2, 2, {2.0f, 0.0f, 2.0f, 0.0f});
        CHECK(data->Weights(2, 2) == glm::vec4(0.5f, 0.0f, 0.5f, 0.0f));
        data->SetWeights(2, 2, glm::vec4(0.0f));
        CHECK(data->Weights(2, 2) == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        // Bloco novo e bloco tirado chegam à textura na próxima chamada; o retângulo do índice os cobre.
        data->AddBlock({-1, 2});
        CHECK(data->WeightsTexture() == texture && texture->BlockCount() == 3 && texture->LayerOf(-1, 2) >= 0);
        CHECK(texture->IndexRect() == glm::ivec4(-1, 0, 3, 3));
        data->RemoveBlock({-1, 2});
        data->WeightsTexture();
        CHECK(texture->BlockCount() == 2 && texture->LayerOf(-1, 2) == -1 && texture->IndexRect() == glm::ivec4(0, 0, 2, 1));

        // Desfazer e refazer a pincelada; ela não mexeu nas alturas.
        const TerrainEdit edit = paint.Finish();
        CHECK(edit.heights.empty() && !edit.weights.empty());
        ApplyTerrainEdit(*data, edit, false);
        CHECK(data->Weights(32, 16) == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        ApplyTerrainEdit(*data, edit, true);
        CHECK(data->Weights(32, 16) == center);

        // Gravar e ler: os pesos voltam (um byte por camada); arquivo de bloco da versão 1 fica na camada 1.
        const fs::path file = base / "Pintado.mterrain";
        std::string error;
        CHECK(data->Save(file, &error));
        const auto loaded = TerrainData::Load(file, &error);
        const glm::vec4 back = loaded ? loaded->Weights(32, 16).value_or(glm::vec4(0.0f)) : glm::vec4(0.0f);
        CHECK(Near(back.y, center.y, 1.0f / 255.0f) && Near(back.x + back.y + back.z + back.w, 1.0f));
        CHECK(loaded && loaded->Weights(35, 16).has_value() && Near(loaded->Weights(35, 16)->y, side.y, 1.0f / 255.0f));
        CHECK(loaded && !loaded->IsDirty());

        const fs::path blockFile = TerrainData::BlocksFolder(file) / "1_0.mtbloco";
        std::string bytes = ReadTextFile(blockFile).value_or("");
        CHECK(bytes.size() == 20 + 33 * 33 * (4 + 4));
        const uint32_t v1 = 1;
        std::memcpy(bytes.data() + 4, &v1, sizeof(v1));
        bytes.resize(20 + 33 * 33 * 4);  // só as alturas, como gravava a Tarefa 8
        {
            std::ofstream out(blockFile, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        error.clear();
        const auto old = TerrainData::Load(file, &error);
        CHECK(old && error.empty());
        CHECK(old && old->Weights(40, 16) == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));  // bloco 1_0 antigo: camada 1
        CHECK(old && old->Weights(16, 16).has_value() && Near(old->Weights(30, 16)->y, data->Weights(30, 16)->y, 1.0f / 255.0f));
    }

    SECTION("Pesos de todos os blocos: camadas reaproveitadas, índice e retângulo");
    {
        TerrainWeights weights(3);
        std::vector<uint8_t> red(3 * 3 * 4, 0);
        for (size_t k = 0; k < red.size(); k += 4) red[k] = 255;
        weights.SetBlock(2, -1, red);
        weights.SetBlock(-3, 4, red);
        CHECK(weights.BlockCount() == 2 && weights.LayerOf(2, -1) == 0 && weights.LayerOf(-3, 4) == 1);
        CHECK(weights.IndexRect() == glm::ivec4(-3, -1, 6, 6));
        weights.SetBlock(2, -1, red);  // o mesmo bloco de novo: a mesma camada
        CHECK(weights.BlockCount() == 2 && weights.LayerOf(2, -1) == 0);
        weights.RemoveBlock(2, -1);
        CHECK(weights.LayerOf(2, -1) == -1 && weights.IndexRect() == glm::ivec4(-3, 4, 1, 1));
        weights.SetBlock(7, 7, red);  // a camada livre volta a ser usada
        CHECK(weights.LayerOf(7, 7) == 0 && weights.BlockCount() == 2);
        weights.RemoveBlock(9, 9);  // não existe: nada muda
        CHECK(weights.BlockCount() == 2);
    }

    SECTION("Camadas do componente: a 1 é o material, 2 a 4 do componente");
    {
        Terrain terrain;
        terrain.material.color = {0.1f, 0.2f, 0.3f, 1.0f};
        terrain.tiling = 2.0f;
        terrain.layer3.tiling = 8.0f;
        const TerrainLayers layers = terrain.Layers();
        CHECK(layers.color[0] == terrain.material.color && layers.tiling[0] == 2.0f && layers.tiling[2] == 8.0f);
        CHECK(layers.color[1] == terrain.layer2.color && layers.color[3] == terrain.layer4.color);
        CHECK(!layers.albedo[1] && !layers.weights);
        auto shared = std::make_shared<Material>();
        shared->color = {0.9f, 0.1f, 0.1f, 1.0f};
        terrain.sharedMaterial = shared;
        CHECK(terrain.Layers().color[0] == shared->color);
    }

    SECTION("Raio no terreno: de cima, inclinado, por baixo e fora");
    {
        TerrainData data = MakeHills(33);
        // Para baixo no meio: acerta na altura do terreno.
        const auto down = data.Raycast({3.3f, 50.0f, -2.7f}, {0.0f, -1.0f, 0.0f}, 1000.0f);
        CHECK(down && Near(down->y, data.HeightAt(3.3f, -2.7f).value_or(0.0f), 1e-3f));
        // Inclinado, de longe: o ponto achado está na superfície.
        const auto slanted = data.Raycast({-40.0f, 20.0f, -2.0f}, {1.0f, -0.5f, 0.05f}, 5000.0f);
        CHECK(slanted && Near(slanted->y, data.HeightAt(slanted->x, slanted->z).value_or(1e9f), 1e-3f));
        // Por baixo, para cima: não acerta (o pincel só pinta o lado de cima).
        CHECK(!data.Raycast({3.3f, -50.0f, -2.7f}, {0.0f, 1.0f, 0.0f}, 1000.0f));
        // Fora dos blocos ou curto demais: nada.
        CHECK(!data.Raycast({500.0f, 50.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 1000.0f));
        CHECK(!data.Raycast({3.3f, 50.0f, -2.7f}, {0.0f, -1.0f, 0.0f}, 10.0f));
    }

    SECTION("Blocos tirados: o arquivo sobra na pasta até o SaveTerrains entregar para o descarte");
    {
        const fs::path file = base / "sobras" / "Campo.mterrain";
        fs::create_directories(file.parent_path());
        auto data = std::make_shared<TerrainData>(8.0f, 9);
        for (int x = -1; x <= 1; ++x) data->AddBlock({x, 0});
        std::string error;
        CHECK(data->Save(file, &error));
        CHECK(data->UnusedBlockFiles(file).empty());
        // Outros arquivos na pasta não são de bloco: ficam.
        const fs::path folder = TerrainData::BlocksFolder(file);
        std::ofstream(folder / "notas.txt") << "x";
        std::ofstream(folder / "abc.mtbloco") << "x";
        CHECK(data->RemoveBlock({-1, 0}));
        CHECK(data->Save(file, &error));
        const auto unused = data->UnusedBlockFiles(file);
        CHECK(unused.size() == 1 && !unused.empty() && unused.front().filename() == "-1_0.mtbloco");

        Scene scene;
        Entity& e = scene.CreateEntity("Terreno");
        Terrain& terrain = e.Add<Terrain>();
        terrain.terrain.file = file;
        terrain.terrain.SetData(data);
        CHECK(data->RemoveBlock({1, 0}));  // mudou: o SaveTerrains grava e entrega os dois que sobraram
        std::vector<fs::path> discarded;
        CHECK(SaveTerrains(scene, &error, [&](const fs::path& p) { discarded.push_back(p); }));
        CHECK(discarded.size() == 2 && fs::exists(folder / "0_0.mtbloco"));
        CHECK(fs::exists(folder / "notas.txt") && fs::exists(folder / "abc.mtbloco"));
    }

    SECTION("Recuperação: só os blocos mudados vão junto, e voltam por cima do terreno do disco depois de uma queda");
    {
        std::string error;
        auto project = Project::Create(base, "Recuperação Ação", &error);
        CHECK(project.has_value());
        if (project) {
            AssetLibrary assets;
            assets.SetRoot(project->RootDir());
            const fs::path file = project->AssetsDir() / "Terrenos" / "Morro.mterrain";
            fs::create_directories(file.parent_path());
            {
                TerrainData data(8.0f, 9);
                for (int z = 0; z < 2; ++z) {
                    for (int x = 0; x < 3; ++x) data.AddBlock({x, z}, 1.0f);
                }
                CHECK(data.Save(file, &error));
            }
            const auto entityWith = [&](Scene& scene) -> Terrain& {
                Terrain& t = scene.CreateEntity("Terreno").Add<Terrain>();
                t.terrain.file = file;
                t.terrain.guid = assets.Database().GuidFor(file);
                t.terrain.path = assets.KeyForFile(file);
                return t;
            };
            std::vector<float> expected;
            {
                Scene scene;
                TerrainData* data = entityWith(scene).terrain.Data();
                CHECK(data && !data->IsDirty());
                if (data) {
                    data->SetSample(3, 3, 7.0f);
                    data->SetWeights(3, 3, {0.0f, 0.0f, 1.0f, 0.0f});
                    data->RemoveBlock({2, 1});
                    data->AddBlock({-1, 0}, 4.0f);
                    expected = *data->BlockHeights({0, 0});
                }
                CHECK(SceneSerializer::SaveRecovery(project->RootDir(), scene, assets, {}, "autosave", &error));
                CHECK(data && data->IsDirty());  // a recuperação não grava o terreno de verdade
                // Só os blocos mudados (o (0, 0), o (1, 0) e o (0, 1) pela borda e o novo), não os 6.
                const fs::path changes = project->RootDir() / ".motor" / "recuperacao" / "terrenos" / "0";
                int files = 0;
                for (const auto& entry : fs::directory_iterator(changes)) {
                    files += LowerExtension(entry.path()) == ".mtbloco";
                }
                CHECK(files > 0 && files < 6);
            }
            // Queda: o terreno some da memória e volta do disco, sem as mudanças.
            Scene recovered;
            TerrainData* data = entityWith(recovered).terrain.Data();
            CHECK(data && data->HasBlock({2, 1}) && !data->HasBlock({-1, 0}) && data->Sample(3, 3) == 1.0f);
            CHECK(SceneSerializer::ApplyRecoveryTerrains(project->RootDir(), recovered, &error) == 1);
            CHECK(data && !data->HasBlock({2, 1}) && data->HasBlock({-1, 0}) && data->IsDirty());
            CHECK(data && data->BlockHeights({0, 0}) && *data->BlockHeights({0, 0}) == expected);
            CHECK(data && data->Weights(3, 3) && data->Weights(3, 3)->z > 0.99f);
            CHECK(data && data->Sample(-4, 0) == 4.0f);
            // Salvo de verdade, o disco fica com as alturas recuperadas.
            CHECK(SaveTerrains(recovered, &error));
            const auto reread = TerrainData::Load(file, &error);
            CHECK(reread && reread->Sample(3, 3) == 7.0f && !reread->HasBlock({2, 1}) && reread->HasBlock({-1, 0}));
        }
    }

    SECTION("Raio da cena no terreno pela grade: o ponto achado está na superfície e bate com o TerrainData");
    {
        Scene scene;
        Entity& e = scene.CreateEntity("Terreno");
        e.transform.position = {5.0f, -1.0f, 3.0f};
        e.Add<Terrain>().terrain.SetData(std::make_shared<TerrainData>(MakeHills(33)));
        const TerrainData& data = *e.Get<Terrain>()->terrain.Data();
        int hits = 0;
        for (int k = 0; k < 40; ++k) {
            // Raios inclinados de vários lados (também quase deitados e em X ou Z puros), de fora do terreno.
            const float angle = static_cast<float>(k) * 0.61f;
            const glm::vec3 dir = glm::normalize(glm::vec3(std::cos(angle), -0.08f - 0.02f * static_cast<float>(k % 9),
                                                           k % 10 == 0 ? 0.0f : std::sin(angle)));
            // Mirando um ponto da superfície (o acerto é ele ou algo antes, num morro do caminho).
            const float tx = -6.0f + static_cast<float>(k % 8) * 2.5f, tz = -6.0f + static_cast<float>(k % 5) * 3.0f;
            const glm::vec3 target(tx, data.HeightAt(tx, tz).value_or(0.0f), tz);
            const glm::vec3 origin = target + glm::vec3(5.0f, -1.0f, 3.0f) - dir * 20.0f;
            const auto hit = scene.Raycast(origin, dir);
            const glm::vec3 local = origin - glm::vec3(5.0f, -1.0f, 3.0f);
            CHECK(hit && hit->entity == e.id && hit->distance <= 20.0f + 1e-3f);
            if (!hit) continue;
            ++hits;
            const glm::vec3 p = local + dir * hit->distance;
            CHECK(Near(p.y, data.HeightAt(p.x, p.z).value_or(1e9f), 1e-3f));
            // Nenhum cruzamento de cima para baixo antes (o TerrainData::Raycast anda de meia célula e perde os de
            // raspão: a conferência é fina, de 1 cm). Entrar por baixo da borda do terreno, de lado, não é cruzamento.
            bool crossed = false;
            std::optional<bool> wasAbove;
            for (float s = 0.0f; s < hit->distance - 0.02f; s += 0.01f) {
                const glm::vec3 q = local + dir * s;
                const auto h = data.HeightAt(q.x, q.z);
                if (!h) {
                    wasAbove.reset();
                    continue;
                }
                const bool isAbove = q.y >= *h - 1e-4f;
                if (wasAbove && *wasAbove && !isAbove) crossed = true;
                wasAbove = isAbove;
            }
            CHECK(!crossed);
        }
        CHECK(hits == 40);
        // De baixo para cima também acerta (o raio da cena vê os dois lados, como o das malhas).
        const auto below = scene.Raycast({8.0f, -50.0f, 4.0f}, {0.0f, 1.0f, 0.0f});
        CHECK(below && Near(-50.0f + below->distance + 1.0f, data.HeightAt(3.0f, 1.0f).value_or(1e9f), 1e-3f));

        // Água: o RaycastWater acha o retângulo dela (o Raycast segue passando por ela até o chão).
        Entity& w = scene.CreateEntity("Água");
        w.transform.position = {5.0f, 10.0f, 3.0f};
        w.Add<Water>().size = {20.0f, 10.0f};
        const auto surface = scene.RaycastWater({8.0f, 30.0f, 4.0f}, {0.0f, -2.0f, 0.0f});
        CHECK(surface && surface->entity == w.id && Near(surface->distance, 10.0f, 1e-4f));
        CHECK(scene.RaycastWater({8.0f, 0.0f, 4.0f}, {0.0f, 1.0f, 0.0f}).has_value());  // de baixo também
        CHECK(!scene.RaycastWater({8.0f, 30.0f, 9.0f}, {0.0f, -1.0f, 0.0f}));  // fora (z de -2 a 8)
        const auto ground = scene.Raycast({8.0f, 30.0f, 4.0f}, {0.0f, -1.0f, 0.0f});
        CHECK(ground && ground->entity == e.id);
        w.active = false;
        CHECK(!scene.RaycastWater({8.0f, 30.0f, 4.0f}, {0.0f, -1.0f, 0.0f}));
    }

    SECTION("Pincelada sem remontar a cena: só os blocos mudados trocam nos estáticos, em todas as entidades do terreno");
    {
        Scene scene;
        auto shared = std::make_shared<TerrainData>(MakeHills(33));  // blocos de 8 m, 0,25 m por célula
        TerrainData& data = *shared;
        Entity& a = scene.CreateEntity("Terreno");
        a.Add<Terrain>().terrain.SetData(shared);
        Entity& b = scene.CreateEntity("Cópia");  // o mesmo terreno em outro lugar
        b.transform.position = {100.0f, 0.0f, 0.0f};
        b.Add<Terrain>().terrain.SetData(shared);
        const auto down = [&](float x, float z) { return scene.Raycast({x, 50.0f, z}, {0.0f, -1.0f, 0.0f}); };
        CHECK(down(4.0f, -4.0f) && scene.StaticRenderCount() == 12);
        // Sem MarkRenderDirty: a cena acha a versão nova do terreno e troca os blocos dela.
        data.SetSample(16, -16, 20.0f);  // x = 4, z = -4
        const auto high = down(4.0f, -4.0f), highCopy = down(104.0f, -4.0f);
        CHECK(high && high->entity == a.id && Near(high->distance, 30.0f, 1e-3f));
        CHECK(highCopy && highCopy->entity == b.id && Near(highCopy->distance, 30.0f, 1e-3f));
        CHECK(scene.StaticRenderCount() == 12);
        // Pintar não muda a malha; tirar e pôr bloco tira e põe só ele.
        data.SetWeights(16, -16, {0.0f, 1.0f, 0.0f, 0.0f});
        CHECK(down(4.0f, -4.0f) && Near(down(4.0f, -4.0f)->distance, 30.0f, 1e-3f));
        CHECK(data.RemoveBlock({0, -1}));
        CHECK(!down(4.0f, -4.0f) && !down(104.0f, -4.0f) && scene.StaticRenderCount() == 10);
        CHECK(down(-4.0f, -4.0f).has_value());  // os outros blocos continuam
        CHECK(data.AddBlock({0, -1}, 3.0f));
        const auto added = down(4.0f, -4.0f);
        CHECK(added && Near(added->distance, 47.0f, 1e-3f) && scene.StaticRenderCount() == 12);
        // O resultado é o mesmo de remontar tudo.
        scene.MarkRenderDirty();
        CHECK(down(4.0f, -4.0f) && Near(down(4.0f, -4.0f)->distance, 47.0f, 1e-3f) && scene.StaticRenderCount() == 12);
    }

    SECTION("LOD: níveis com a borda inteira do bloco, sem buracos nem vértices em T");
    {
        TerrainData data = MakeHills(33);  // blocos de 8 m, 32 células
        const std::shared_ptr<Mesh> full = data.BlockMesh({0, -1});
        CHECK(full && full->Lods().size() == 4);
        const std::vector<float>& heights = *data.BlockHeights({0, -1});
        const float spacing = data.Spacing(), size = data.BlockSize();
        size_t previous = full ? full->Indices().size() : 0;
        for (const Mesh::Lod& lod : full ? full->Lods() : std::vector<Mesh::Lod>{}) {
            const std::vector<glm::vec3>& p = lod.mesh->Positions();
            const std::vector<uint32_t>& idx = lod.mesh->Indices();
            CHECK(idx.size() < previous);  // cada nível com menos triângulos
            previous = idx.size();
            // Triângulos virados para cima, cobrindo a área do bloco uma vez só.
            float area = 0.0f;
            bool up = true;
            std::map<std::pair<uint32_t, uint32_t>, int> edges;
            for (size_t t = 0; t + 2 < idx.size(); t += 3) {
                const glm::vec3 a = p[idx[t]], b = p[idx[t + 1]], c = p[idx[t + 2]];
                const float y = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);  // cross((b - a), (c - a)).y
                up = up && y > 0.0f;
                area += 0.5f * y;
                for (int k = 0; k < 3; ++k) {
                    const uint32_t u = idx[t + k], v = idx[t + (k + 1) % 3];
                    ++edges[{std::min(u, v), std::max(u, v)}];
                }
            }
            CHECK(up && Near(area, size * size, 1e-2f));
            // Arestas usadas por um triângulo só = a borda do bloco, nos 4 x 32 pedaços da malha cheia; as de dentro
            // são de dois triângulos (sem fresta nem T dentro do bloco). As da borda, com as alturas das amostras.
            int border = 0;
            bool edgesOk = true, heightsOk = true;
            for (const auto& [edge, count] : edges) {
                const glm::vec3 a = p[edge.first], b = p[edge.second];
                const bool onBorder = (Near(a.x, b.x) && (Near(a.x, 0.0f) || Near(a.x, size))) ||
                                      (Near(a.z, b.z) && (Near(a.z, 0.0f) || Near(a.z, size)));
                if (count == 1) {
                    ++border;
                    edgesOk = edgesOk && onBorder && Near(glm::length(glm::vec2(b.x - a.x, b.z - a.z)), spacing);
                    for (const glm::vec3& q : {a, b}) {
                        const int i = static_cast<int>(std::lround(q.x / spacing));
                        const int j = static_cast<int>(std::lround(q.z / spacing));
                        heightsOk = heightsOk && q.y == heights[static_cast<size_t>(j * 33 + i)];
                    }
                } else {
                    edgesOk = edgesOk && count == 2;
                }
            }
            std::printf("  nível abaixo de %.4f da tela: %zu triângulos, %zu vértices\n", lod.screenSize, idx.size() / 3,
                        p.size());
            CHECK(edgesOk && heightsOk && border == 4 * 32);
        }
        // O nível sai do tamanho na tela; perto, a malha cheia.
        CHECK(full && &full->ForScreenSize(1.0f) == full.get());
        CHECK(full && &full->ForScreenSize(0.01f) == full->Lods().back().mesh.get());
        // Resolução 9 (8 células): só os passos 2 e 4.
        TerrainData small = MakeHills(9);
        CHECK(small.BlockMesh({0, 0})->Lods().size() == 2);
    }
    return TestResult();
}
