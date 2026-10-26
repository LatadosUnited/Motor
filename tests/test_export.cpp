// Exportar o jogo (Fase 10, editor/src/GameExport): lista dos assets referenciados e a pasta exportada, sem
// GPU e sem o Player de verdade (arquivos falsos no lugar do .exe e das DLLs).
#include "TestUtil.h"

#include "export/GameExport.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/core/Paths.h>
#include <motor/project/Project.h>
#include <motor/renderer/Texture.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneSerializer.h>
#include <motor/ui/UiComponents.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace motor;
using namespace editor;

namespace {

int g_fakeTexture = 0;

void Write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::string Read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// BMP 1x1 de verdade (o stb_image decodifica): a textura do material é lida ao carregar o .mmat.
std::string TinyBmp() {
    const uint8_t header[54] = {'B', 'M', 58, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0,
                                24, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::string bytes(reinterpret_cast<const char*>(header), sizeof(header));
    bytes += std::string("\x20\x40\xC0\x00", 4);
    return bytes;
}

bool Has(const std::vector<std::string>& list, const std::string& item) {
    return std::find(list.begin(), list.end(), item) != list.end();
}

} // namespace

int main(int argc, char** argv) {
    const fs::path base = fs::absolute(TestDir(argc, argv, "test_export_tmp")).lexically_normal();
    std::string error;
    auto project = Project::Create(base, "Jogo Ação", &error);
    CHECK(project.has_value());
    if (!project) return TestResult();
    const fs::path root = project->RootDir();
    const fs::path assetsDir = project->AssetsDir();

    // --- Arquivos do projeto ---
    Write(assetsDir / "Modelos" / "caixa.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    Write(assetsDir / "Modelos" / "sobra.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    Write(assetsDir / "Texturas" / "tijolo.bmp", TinyBmp());
    Write(assetsDir / "Texturas" / "sobra.bmp", TinyBmp());
    Write(assetsDir / PathFromUtf8("Fontes/Título.ttf"), "fonte falsa");
    Write(assetsDir / "Modelos" / "robo.bin", "buffer");
    Write(assetsDir / "Modelos" / "robo cor.png", "png falso");
    Write(assetsDir / "Modelos" / "robo.gltf",
          R"({"asset":{"version":"2.0"},"buffers":[{"uri":"robo.bin","byteLength":6}],)"
          R"("images":[{"uri":"robo%20cor.png"},{"uri":"data:image/png;base64,AAAA"}]})");
    Write(assetsDir / "Recursos" / "falas.txt", "texto lido pelo script");
    Write(assetsDir / "Sons" / "pulo.wav", "RIFF falso");  // Fase 8: som citado pelo caminho num script
    Write(assetsDir / "Sons" / "sobra.wav", "RIFF falso");
    Write(assetsDir / PathFromUtf8("Sons/Música.ogg"), "OggS falso");  // Fase 8: clipe de uma Fonte de áudio
    Write(root / "Scripts" / "Jogador.cpp", "// toca \"Assets/Sons/pulo.wav\"\nconst char* kSom = \"Assets/Sons/pulo.wav\";\n"
                                          "const char* kNada = \"Assets/Sons/nao-existe.wav\";\n");

    AssetLibrary assets;
    assets.SetTextureFactory([](const ImageData&) {
        return std::shared_ptr<Texture2D>(reinterpret_cast<Texture2D*>(&g_fakeTexture), [](Texture2D*) {});
    });
    assets.SetRoot(root);
    AssetDatabase& db = assets.Database();
    const std::string bmpGuid = db.GuidFor(assetsDir / "Texturas" / "tijolo.bmp");
    const std::string gltfGuid = db.GuidFor(assetsDir / "Modelos" / "robo.gltf");
    const std::string fontGuid = db.GuidFor(assetsDir / PathFromUtf8("Fontes/Título.ttf"));
    const std::string musicGuid = db.GuidFor(assetsDir / PathFromUtf8("Sons/Música.ogg"));
    CHECK(!bmpGuid.empty() && !gltfGuid.empty() && !fontGuid.empty() && !musicGuid.empty());
    Write(assetsDir / "Navegacao" / "Navmesh.mnav", "MNAV falso");  // Fase 34: navmesh da Superfície de navegação
    const std::string navGuid = db.GuidFor(assetsDir / "Navegacao" / "Navmesh.mnav");
    CHECK(!navGuid.empty());

    // Material com textura (gravado pela engine) e prefab que usa uma malha do glTF.
    const fs::path matFile = assetsDir / "Materiais" / "Tijolo.mmat";
    fs::create_directories(matFile.parent_path());
    MaterialTextureRefs refs;
    refs.albedo = AssetRef{"Assets/Texturas/tijolo.bmp", bmpGuid};
    CHECK(assets.WriteMaterialFile(matFile, Material{}, refs, &error));
    const fs::path prefabFile = assetsDir / "Prefabs" / "Robo.mprefab";
    Write(prefabFile, std::string(R"({"format":"motor-prefab","version":1,"entities":[{"name":"Robo","components":)") +
                          R"({"MeshRenderer":{"mesh":{"guid":")" + gltfGuid + R"(#Corpo","path":"Assets/Modelos/robo.gltf#Corpo"}}}}]})");
    const std::string prefabGuid = db.GuidFor(prefabFile);

    // Cena inicial gravada pelo SceneSerializer: malha .obj, material, texto com fonte do projeto.
    {
        Scene scene;
        Entity& box = scene.CreateEntity("Caixa");
        MeshRenderer& mr = box.Add<MeshRenderer>();
        mr.mesh = assets.GetMesh("Assets/Modelos/caixa.obj", &error);
        mr.sharedMaterial = assets.GetMaterial("Assets/Materiais/Tijolo.mmat", &error);
        CHECK(mr.mesh != nullptr);
        CHECK(mr.sharedMaterial != nullptr);
        Entity& label = scene.CreateEntity("Pontos");
        label.Add<UiRect>();
        label.Add<UiText>().font = UiFont{fontGuid, "Assets/Fontes/Título.ttf"};
        scene.CreateEntity("Música").Add<AudioSource>().clip = AudioClip{musicGuid, "Assets/Sons/Música.ogg", {}};
        scene.CreateEntity("Navegação").Add<NavMeshSurface>().navMesh = NavMeshData{navGuid, "Assets/Navegacao/Navmesh.mnav", {}};
        fs::create_directories(assetsDir / "Cenas");
        CHECK(SceneSerializer::Save(assetsDir / "Cenas" / "Inicio.mscene", scene, assets, &error));
    }
    // Segunda cena (só carregada por LoadScene): instância do prefab e uma referência quebrada.
    Write(assetsDir / "Cenas" / "Fase2.mscene",
          std::string(R"({"format":"motor-scene","version":1,"entities":[{"name":"Robo","components":{"PrefabInstance":)") +
              R"({"prefab":{"guid":")" + prefabGuid + R"("}},"MeshRenderer":{"mesh":{"guid":"0123456789abcdef0123456789abcdef",)" +
              R"("path":"Assets/Modelos/apagado.obj"}}}}]})");
    db.Refresh();

    SECTION("Assets referenciados");
    const GameExport::Collection c = GameExport::CollectAssets(*project, db);
    for (const std::string& f : c.files) std::printf("    %s\n", f.c_str());
    CHECK(Has(c.files, "Assets/Cenas/Inicio.mscene"));
    CHECK(Has(c.files, "Assets/Cenas/Fase2.mscene"));         // todas as cenas
    CHECK(Has(c.files, "Assets/Modelos/caixa.obj"));          // malha pelo GUID
    CHECK(Has(c.files, "Assets/Materiais/Tijolo.mmat"));      // material
    CHECK(Has(c.files, "Assets/Texturas/tijolo.bmp"));        // textura do material
    CHECK(Has(c.files, "Assets/Fontes/Título.ttf"));          // fonte da UI
    CHECK(Has(c.files, "Assets/Prefabs/Robo.mprefab"));       // prefab da Fase2
    CHECK(Has(c.files, "Assets/Modelos/robo.gltf"));          // modelo do prefab ("<guid>#Corpo")
    CHECK(Has(c.files, "Assets/Modelos/robo.bin"));           // buffer do glTF
    CHECK(Has(c.files, "Assets/Modelos/robo cor.png"));       // imagem do glTF (uri com %20)
    CHECK(Has(c.files, "Assets/Recursos/falas.txt"));         // pasta Recursos inteira
    CHECK(Has(c.files, "Assets/Sons/pulo.wav"));              // citado num script
    CHECK(Has(c.files, "Assets/Sons/Música.ogg"));            // clipe de uma Fonte de áudio (Fase 8)
    CHECK(Has(c.files, "Assets/Navegacao/Navmesh.mnav"));     // navmesh da Superfície de navegação (Fase 34)
    CHECK(!Has(c.files, "Assets/Modelos/sobra.obj"));
    CHECK(!Has(c.files, "Assets/Texturas/sobra.bmp"));
    CHECK(!Has(c.files, "Assets/Sons/sobra.wav"));
    CHECK(!Has(c.files, "Assets/Sons/nao-existe.wav"));
    CHECK(std::none_of(c.files.begin(), c.files.end(), [](const std::string& f) { return f.ends_with(".meta"); }));
    CHECK(c.missing.size() == 1);
    if (!c.missing.empty()) CHECK(c.missing[0].find("0123456789abcdef0123456789abcdef") != std::string::npos);

    // --- Exportar com um Player e DLLs falsos ---
    const fs::path fake = base / "falso";
    Write(fake / "bin" / "Player.exe", "MZ player");
    for (const char* dll : GameExport::kRuntimeDlls) Write(fake / "bin" / dll, "MZ dll");
    Write(fake / "engine" / "shaders" / "lit.vert", "#version 450");
    Write(fake / "engine" / "fonts" / "Roboto.ttf", "fonte");
    Write(fake / "GameScripts.dll", "MZ scripts");

    GameExport::Options options;
    options.startupScene = "Assets/Cenas/Inicio.mscene";
    options.outputDir = base / PathFromUtf8("Saída do Jogo");
    options.playerExe = fake / "bin" / "Player.exe";
    options.runtimeDir = fake / "bin";
    options.engineAssetsDir = fake / "engine";
    options.scriptsDll = fake / "GameScripts.dll";
    options.exeResources = false;  // Player falso: sem recursos para gravar (test_exe_identity grava num .exe de verdade)

    SECTION("Exportar");
    GameExport::Result r = GameExport::Export(*project, db, options);
    if (!r.ok) std::printf("  erro: %s\n", r.error.c_str());
    CHECK(r.ok);
    const fs::path out = options.outputDir;
    const fs::path data = out / GameExport::kDataFolder;
    CHECK(r.executable == out / PathFromUtf8("Jogo Ação.exe"));
    CHECK(Read(r.executable) == "MZ player");
    CHECK(fs::is_regular_file(out / "libMotor.dll"));
    CHECK(fs::is_regular_file(out / "libstdc++-6.dll"));
    CHECK(fs::is_regular_file(out / "engine" / "shaders" / "lit.vert"));
    CHECK(fs::is_regular_file(out / "engine" / "fonts" / "Roboto.ttf"));
    CHECK(fs::is_regular_file(data / GameExport::kScriptsDllName));
    CHECK(fs::is_regular_file(out / GameExport::kMarkerFile));
    CHECK(fs::is_regular_file(data / PathFromUtf8("Assets/Fontes/Título.ttf")));
    CHECK(fs::is_regular_file(data / "Assets" / "Modelos" / "robo cor.png"));
    CHECK(!fs::exists(data / "Assets" / "Modelos" / "sobra.obj"));
    CHECK(!fs::exists(data / "Scripts"));  // os fontes não vão, só a DLL
    CHECK(r.assets.size() == c.files.size());
    CHECK(r.warnings.size() == 1);
    CHECK(r.bytes > 0);
    // O .meta vai junto: o GUID no jogo é o mesmo do projeto.
    CHECK(Read(data / "Assets" / "Texturas" / "tijolo.bmp.meta") == Read(assetsDir / "Texturas" / "tijolo.bmp.meta"));

    SECTION("Projeto exportado abre com a cena inicial e acha os assets pelo GUID");
    auto game = Project::Open(data, &error);
    CHECK(game.has_value());
    if (game) {
        CHECK(game->Name() == "Jogo Ação");
        CHECK(game->StartupScene() == (data / "Assets" / "Cenas" / "Inicio.mscene").lexically_normal());
        AssetLibrary gameAssets;
        gameAssets.SetTextureFactory([](const ImageData&) {
            return std::shared_ptr<Texture2D>(reinterpret_cast<Texture2D*>(&g_fakeTexture), [](Texture2D*) {});
        });
        gameAssets.SetRoot(game->RootDir());
        CHECK(gameAssets.Database().PathFor(bmpGuid) == (data / "Assets" / "Texturas" / "tijolo.bmp").lexically_normal());
        Scene loaded;
        CHECK(SceneSerializer::Load(game->StartupScene(), loaded, gameAssets, &error));
        const Entity* box = loaded.FindByName("Caixa");
        CHECK(box && box->Get<MeshRenderer>() && box->Get<MeshRenderer>()->mesh && box->Get<MeshRenderer>()->sharedMaterial);
    }

    SECTION("Exportar de novo na mesma pasta limpa a anterior");
    Write(data / "Assets" / "velho.txt", "de uma exportação antiga");
    Write(out / "save.dat", "arquivo do jogador");  // fora da lista da marca: fica
    options.startupScene = "Assets/Cenas/Fase2.mscene";
    r = GameExport::Export(*project, db, options);
    CHECK(r.ok);
    CHECK(!fs::exists(data / "Assets" / "velho.txt"));
    CHECK(fs::is_regular_file(out / "save.dat"));
    if (auto again = Project::Open(data, &error)) {
        CHECK(again->ToRelative(again->StartupScene()) == "Assets/Cenas/Fase2.mscene");
    }

    SECTION("Recusas");
    GameExport::Options bad = options;
    bad.outputDir = base / "outra";
    Write(bad.outputDir / "meu.txt", "não é exportação");
    r = GameExport::Export(*project, db, bad);
    CHECK(!r.ok && r.error.find("não está vazia") != std::string::npos);
    CHECK(fs::is_regular_file(bad.outputDir / "meu.txt"));

    bad = options;
    bad.outputDir = assetsDir / "Build";
    CHECK(!GameExport::Export(*project, db, bad).ok);
    CHECK(!fs::exists(assetsDir / "Build"));

    bad = options;
    bad.outputDir = base;  // conteria o projeto
    CHECK(!GameExport::Export(*project, db, bad).ok);

    bad = options;
    bad.startupScene = "Assets/Cenas/NaoExiste.mscene";
    CHECK(!GameExport::Export(*project, db, bad).ok);

    bad = options;
    bad.startupScene.clear();  // e o projeto sem cena inicial
    CHECK(!GameExport::Export(*project, db, bad).ok);

    bad = options;
    bad.outputDir = base / "nova";
    bad.playerExe = fake / "nao-existe.exe";
    r = GameExport::Export(*project, db, bad);
    CHECK(!r.ok && r.error.find("Player") != std::string::npos);
    CHECK(!fs::exists(bad.outputDir));  // nada criado quando falta alguma peça

    bad = options;
    bad.outputDir = base / "nova";
    bad.scriptsDll = fake / "nao-compilada.dll";
    CHECK(!GameExport::Export(*project, db, bad).ok);

    SECTION("Pasta padrão");
    CHECK(GameExport::DefaultOutputDir(*project) == root / "Exportado");

    return TestResult();
}
