// Documentação dos scripts (docs/scripts.md, docs/scripts-api.md): para o guia não ficar desatualizado.
// - Compila numa DLL de scripts, com o toolchain do Motor, todos os exemplos do guia (docs/examples/) e os
//   modelos do editor (editor/templates/Scripts/), sem erros nem avisos.
// - Roda os exemplos de verdade: o mini jogo do guia (andar, coletar, pontuar), a porta, o inimigo, o
//   disparador e a bola, com a física, simulando as teclas no estado interno do Input; o HUD e o menu com a
//   UI do jogo (UiSystem), simulando o mouse; os sons do menu com o áudio (AudioSystem) sem dispositivo; o
//   guarda com a navegação (NavSystem).
// - Cada bloco ```cpp do guia é um trecho de um arquivo de exemplo, e cada exemplo é citado no guia.
// - A referência cita todos os métodos públicos de Script, de PhysicsSystem, de UiSystem, de AudioSystem, de
//   NavSystem e as opções de FieldOptions.
// Sem janela nem OpenGL. Demora alguns segundos (compila a DLL).
#include "TestUtil.h"

#include "scripts/ScriptBuilder.h"
#include "scripts/ScriptHost.h"
#include "scripts/ScriptProject.h"

#include <motor/ai/NavSystem.h>
#include <motor/assets/AssetLibrary.h>
#include <motor/audio/AudioSystem.h>
#include <motor/core/Input.h>
#include <motor/core/Paths.h>
#include <motor/physics/PhysicsSystem.h>
#include <motor/scene/Components.h>
#include <motor/scene/Prefab.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/scene/ScriptModule.h>
#include <motor/ui/UiSystem.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <fstream>
#include <functional>
#include <optional>
#include <regex>
#include <utility>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace motor;
using namespace editor;
namespace fs = std::filesystem;
namespace SP = editor::ScriptProject;

namespace {

constexpr float kDt = 1.0f / 60.0f;

std::string ReadText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> lines;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

bool Contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

bool Near(float a, float b, float eps) { return std::abs(a - b) < eps; }

bool BuildAndWait(ScriptBuilder& builder, const SP::Paths& paths, const SP::Toolchain& toolchain) {
    builder.Start(paths, toolchain);
    const auto start = std::chrono::steady_clock::now();
    while (!builder.Poll()) {
        if (std::chrono::steady_clock::now() - start > std::chrono::minutes(8)) {
            builder.Cancel();
            std::printf("  (tempo esgotado)\n");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const bool ok = builder.GetState() == ScriptBuilder::State::Succeeded;
    if (!ok || !SP::ParseDiagnostics(builder.Log()).empty()) {
        std::printf("----- saída da compilação -----\n%s\n-------------------------------\n", builder.Log().c_str());
    }
    std::printf("  (compilação: %.1f s)\n", builder.Seconds());
    return ok;
}

// --- Blocos de código do guia -------------------------------------------------------

struct CodeBlock {
    int line = 0;  // linha do ``` no guia
    std::vector<std::string> lines;
};

// Blocos ```cpp (só esses: ```cpp errado, ```text etc. não são conferidos).
std::vector<CodeBlock> CppBlocks(const std::string& markdown) {
    std::vector<CodeBlock> blocks;
    const std::vector<std::string> lines = Lines(markdown);
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i] != "```cpp") continue;
        CodeBlock block;
        block.line = static_cast<int>(i + 1);
        for (++i; i < lines.size() && lines[i] != "```"; ++i) block.lines.push_back(lines[i]);
        blocks.push_back(std::move(block));
    }
    return blocks;
}

// `part` aparece em `file` como linhas seguidas, a partir de `from`. Devolve a linha seguinte ao trecho.
std::optional<size_t> FindLines(const std::vector<std::string>& file, const std::vector<std::string>& part, size_t from) {
    if (part.empty()) return from;
    for (size_t i = from; i + part.size() <= file.size(); ++i) {
        if (std::equal(part.begin(), part.end(), file.begin() + static_cast<std::ptrdiff_t>(i))) return i + part.size();
    }
    return std::nullopt;
}

// O bloco é um trecho do arquivo: as partes separadas por linhas "// ..." aparecem nele, nessa ordem.
bool BlockIsExcerptOf(const CodeBlock& block, const std::vector<std::string>& file) {
    std::vector<std::vector<std::string>> parts(1);
    for (const std::string& line : block.lines) {
        if (line.find_first_not_of(' ') != std::string::npos && line.substr(line.find_first_not_of(' ')) == "// ...") {
            parts.emplace_back();
        } else {
            parts.back().push_back(line);
        }
    }
    size_t at = 0;
    for (const auto& part : parts) {
        const auto next = FindLines(file, part, at);
        if (!next) return false;
        at = *next;
    }
    return true;
}

// Nomes de funções declaradas num trecho de header (o que vem antes de '(' numa declaração).
std::set<std::string> DeclaredFunctions(const std::string& code) {
    std::set<std::string> names;
    static const std::regex decl(R"((?:^|\s|[*&>])([A-Z]\w*)\s*\()");
    for (const std::string& line : Lines(code)) {
        const std::string trimmed = line.substr(std::min(line.size(), line.find_first_not_of(' ')));
        if (trimmed.starts_with("//") || trimmed.starts_with("return") || trimmed.starts_with("static_assert") ||
            trimmed.starts_with("WriteLog")) {
            continue;
        }
        for (std::sregex_iterator it(line.begin(), line.end(), decl), end; it != end; ++it) names.insert((*it)[1]);
    }
    return names;
}

// Texto entre `begin` e o primeiro `end` depois dele.
std::string Between(const std::string& text, const std::string& begin, const std::string& end) {
    const size_t a = text.find(begin);
    if (a == std::string::npos) return {};
    const size_t b = text.find(end, a + begin.size());
    return text.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

// --- Cena e scripts da DLL ------------------------------------------------------------

// Componente de script registrado pela DLL, adicionado à entidade.
void* AddScript(Entity& entity, const char* type) {
    const ComponentType* t = ComponentRegistry::Instance().Find(type);
    return t ? entity.Add(*t) : nullptr;
}

// Campo registrado de um script (para ler e mudar valores, como o Inspector faz).
template <class F>
F* ScriptField(Scene& scene, EntityId id, const char* type, const char* key) {
    const ComponentType* t = ComponentRegistry::Instance().Find(type);
    Entity* entity = scene.Find(id);
    if (!t || !entity) return nullptr;
    void* data = entity->Get(*t);
    if (!data) return nullptr;
    for (const FieldInfo& f : t->fields) {
        if (f.key == key) return &f.As<F>(data);
    }
    return nullptr;
}

// Chão estático de 20 x 1 x 20 com o topo em y = 0.
void AddFloor(Scene& scene) {
    Entity& floor = scene.CreateEntity("Chão");
    floor.transform.position = {0.0f, -0.5f, 0.0f};
    floor.transform.scale = {20.0f, 1.0f, 20.0f};
    floor.Add<ColliderComponent>();
}

EntityId AddPlayer(Scene& scene, const glm::vec3& position, bool withScript = true) {
    Entity& player = scene.CreateEntity("Jogador");
    player.transform.position = position;
    player.Add<CharacterControllerComponent>();  // 1.8 m: centro em y = 0.9 no chão
    if (withScript) AddScript(player, "Jogador");
    return player.id;
}

EntityId AddTrigger(Scene& scene, const char* name, const glm::vec3& position, const glm::vec3& size, const char* script) {
    Entity& e = scene.CreateEntity(name);
    e.transform.position = position;
    ColliderComponent& c = e.Add<ColliderComponent>();
    c.size = size;
    c.isTrigger = true;
    AddScript(e, script);
    return e.id;
}

// Um quadro do editor: passo fixo, Update e o Input avança (WasKeyPressed vale só no primeiro quadro).
void Run(SceneRuntime& runtime, float seconds) {
    const int steps = static_cast<int>(std::lround(seconds / kDt));
    for (int i = 0; i < steps; ++i) {
        runtime.FixedUpdate(kDt);
        runtime.Update(kDt);
        Input::BeginFrame();
    }
}

void Hold(Key key, bool down) {
    Input::SetKeyState(key, down);
}

int CountNamed(const Scene& scene, const std::string& name) {
    return static_cast<int>(std::count_if(scene.Entities().begin(), scene.Entities().end(),
                                          [&](const auto& e) { return e->name == name; }));
}

} // namespace

int main(int argc, char** argv) {
    const fs::path dir = TestDir(argc, argv, "tmp_test_script_docs");
    const fs::path docs = MOTOR_TEST_DOCS_DIR;
    const fs::path examples = docs / "examples";
    const fs::path templates = MOTOR_TEST_TEMPLATES_DIR;
    const fs::path root = dir / PathFromUtf8("Jogo Ação");
    const SP::Paths paths = SP::PathsFor(root);

    // Arquivos de exemplo: os do guia e os modelos do editor (que o guia também usa).
    std::vector<fs::path> exampleFiles;
    for (const fs::path& folder : {examples, templates}) {
        for (const auto& entry : fs::directory_iterator(folder)) {
            if (SP::IsScriptFile(entry.path())) exampleFiles.push_back(entry.path());
        }
    }
    std::sort(exampleFiles.begin(), exampleFiles.end());

    SECTION("O guia cita os exemplos e os seus blocos de código são trechos deles");
    const std::string guide = ReadText(docs / "scripts.md");
    CHECK(!guide.empty());
    for (const fs::path& file : exampleFiles) {
        const std::string name = ToUtf8(file.filename());
        const bool cited = Contains(guide, "examples/" + name) || Contains(guide, "templates/Scripts/" + name);
        if (!cited) std::printf("  não citado no guia: %s\n", name.c_str());
        CHECK(cited);
    }
    {
        std::vector<std::vector<std::string>> fileLines;
        for (const fs::path& file : exampleFiles) fileLines.push_back(Lines(ReadText(file)));
        const auto blocks = CppBlocks(guide);
        CHECK(blocks.size() >= 10);
        for (const CodeBlock& block : blocks) {
            const bool found = std::any_of(fileLines.begin(), fileLines.end(),
                                           [&](const auto& lines) { return BlockIsExcerptOf(block, lines); });
            if (!found) std::printf("  bloco da linha %d de docs/scripts.md não está em nenhum exemplo\n", block.line);
            CHECK(found);
        }
    }

    SECTION("A referência cita toda a API pública dos scripts");
    {
        const std::string reference = ReadText(docs / "scripts-api.md");
        CHECK(!reference.empty());
        const fs::path include = fs::path(MOTOR_TEST_ENGINE_INCLUDE) / "motor";
        const std::string scriptH = ReadText(include / "scene" / "Script.h");
        const std::string physicsH = ReadText(include / "physics" / "PhysicsSystem.h");
        const std::string componentH = ReadText(include / "scene" / "Component.h");
        const std::string uiH = ReadText(include / "ui" / "UiSystem.h");
        const std::string audioH = ReadText(include / "audio" / "AudioSystem.h");
        const std::string optionsH = ReadText(include / "project" / "GameOptions.h");
        const std::string saveH = ReadText(include / "project" / "SaveData.h");
        const std::string navH = ReadText(include / "ai" / "NavSystem.h");
        const std::string perceptionH = ReadText(include / "ai" / "PerceptionSystem.h");

        std::set<std::string> names = DeclaredFunctions(Between(scriptH, "class Script {", "private:"));
        for (const std::string& n : DeclaredFunctions(Between(physicsH, "class PhysicsSystem", "private:"))) names.insert(n);
        for (const std::string& n : DeclaredFunctions(Between(uiH, "class UiSystem", "private:"))) names.insert(n);
        for (const std::string& n : DeclaredFunctions(Between(audioH, "class AudioSystem", "private:"))) names.insert(n);
        for (const std::string& n : DeclaredFunctions(Between(saveH, "class SaveData {", "private:"))) names.insert(n);
        for (const std::string& n : DeclaredFunctions(Between(navH, "class NavSystem", "private:"))) names.insert(n);
        for (const std::string& n : DeclaredFunctions(Between(perceptionH, "class PerceptionSystem", "private:")))
            names.insert(n);
        names.erase("NavSystem");
        names.erase("SaveData");
        names.erase("Set");  // chamada dentro dos SetInt/SetFloat... (inline), não é método público
        names.erase("Script");
        names.erase("PhysicsSystem");
        names.erase("UiSystem");
        names.erase("AudioSystem");
        names.erase("FindSystem");  // detalhe de GetSystem
        CHECK(names.size() >= 30);
        for (const std::string& n : names) {
            const bool documented = Contains(reference, "`" + n) || Contains(reference, n + "(");
            if (!documented) std::printf("  sem documentação: %s\n", n.c_str());
            CHECK(documented);
        }
        // Opções de campo e campos das structs que os scripts recebem.
        const std::regex member(R"((\w+)\s*(?:=|\{))");
        const std::pair<const std::string*, const char*> structs[] = {
            {&componentH, "struct FieldOptions {"}, {&scriptH, "struct GameTime {"}, {&scriptH, "struct Collision {"},
            {&uiH, "struct UiPointer {"}, {&audioH, "struct AudioSettings {"}, {&audioH, "struct AudioVoiceInfo {"},
            {&optionsH, "struct GameOptions {"}, {&navH, "struct NavStats {"},
            {&perceptionH, "struct PerceptionStats {"}, {&perceptionH, "struct PerceptionEye {"},
            {&perceptionH, "struct HeardNoise {"}};
        for (const auto& [header, type] : structs) {
            const std::string body = Between(*header, type, "};");
            CHECK(!body.empty());
            for (const std::string& line : Lines(body)) {
                std::smatch m;
                if (line.starts_with("    ") && std::regex_search(line, m, member)) {
                    const bool documented = Contains(reference, "`" + m[1].str());
                    if (!documented) std::printf("  sem documentação: %s.%s\n", type, m[1].str().c_str());
                    CHECK(documented);
                }
            }
        }
        CHECK(Contains(reference, std::format("kScriptApiVersion` = {}", kScriptApiVersion)));
    }

    SECTION("Compilar os exemplos e os modelos, sem erros nem avisos");
    fs::create_directories(paths.scriptsDir);
    for (const fs::path& file : exampleFiles) {
        CHECK(!fs::exists(paths.scriptsDir / file.filename()));  // nomes únicos entre exemplos e modelos
        fs::copy_file(file, paths.scriptsDir / file.filename());
    }
    // Os modelos de "Novo script..." (gerados pelo editor) também compilam sem avisos.
    std::vector<std::string> newScriptTypes;
    for (const SP::NewScriptTemplate& t : SP::NewScriptTemplates()) {
        newScriptTypes.push_back(std::string("Modelo_") + t.id);
        CHECK(SP::CreateScript(paths.scriptsDir, newScriptTypes.back(), nullptr, nullptr, t.id));
    }
    std::string toolchainError;
    const auto toolchain = SP::LoadToolchain(MOTOR_TEST_SDK_JSON, &toolchainError);
    CHECK(toolchain.has_value());
    if (!toolchain) {
        std::printf("  %s\n", toolchainError.c_str());
        return TestResult();
    }
    ScriptBuilder builder;
    CHECK(BuildAndWait(builder, paths, *toolchain));
    CHECK(SP::ParseDiagnostics(builder.Log()).empty());

    AssetLibrary assets;
    std::vector<Scene*> loadedScenes;  // cenas que o host atualiza ao descarregar
    auto scenes = [&](const std::function<void(Scene&)>& fn) {
        for (Scene* s : loadedScenes) fn(*s);
    };
    ScriptHost host;
    const ScriptHost::Result loaded = host.Load(paths.dll, paths.loadDir, scenes, assets);
    CHECK(loaded.ok);
    if (!loaded.ok) {
        std::printf("  %s\n", loaded.error.c_str());
        return TestResult();
    }
    for (const char* type : {"Flutuar", "ExemploCampos", "CicloDeVida", "Projetil", "Disparador", "Placar", "Moeda",
                             "Porta", "Inimigo", "BolaFisica", "Hud", "MenuPrincipal", "SonsDoMenu", "Girar", "Jogador", "MoverWASD", "Seguir",
                             "Lampada", "Interruptor", "Chuva", "Guarda", "Pastor"}) {
        const ComponentType* t = ComponentRegistry::Instance().Find(type);
        if (!t) std::printf("  não registrado: %s\n", type);
        CHECK(t && t->IsScript());
    }
    for (const std::string& type : newScriptTypes) {
        const ComponentType* t = ComponentRegistry::Instance().Find(type);
        if (!t) std::printf("  não registrado: %s\n", type.c_str());
        CHECK(t && t->IsScript());
    }

    SECTION("Mini jogo do guia: anda com WASD, pega as moedas e o placar soma");
    {
        Scene scene;
        loadedScenes = {&scene};
        AddFloor(scene);
        const EntityId player = AddPlayer(scene, {0.0f, 0.9f, 0.0f});
        AddTrigger(scene, "Moeda", {2.0f, 1.0f, 0.0f}, glm::vec3(0.8f), "Moeda");
        AddTrigger(scene, "Moeda", {4.0f, 1.0f, 0.0f}, glm::vec3(0.8f), "Moeda");
        AddTrigger(scene, "Moeda", {0.0f, 1.0f, 6.0f}, glm::vec3(0.8f), "Moeda");  // fora do caminho
        Entity& placarEntity = scene.CreateEntity("Placar");
        AddScript(placarEntity, "Placar");
        const EntityId placar = placarEntity.id;
        Entity& camera = scene.CreateEntity("Câmera");
        camera.Add<CameraComponent>();
        AddScript(camera, "Seguir");
        const EntityId cameraId = camera.id;

        SceneRuntime runtime(scene);
        runtime.Start();
        Run(runtime, 0.5f);
        CHECK(CountNamed(scene, "Moeda") == 3);
        Hold(Key::D, true);  // D anda para +X
        Run(runtime, 1.5f);  // 4 m/s: passa pelas duas moedas
        Hold(Key::D, false);
        Run(runtime, 0.2f);
        const glm::vec3 p = scene.Find(player)->transform.position;
        std::printf("  jogador em x = %.2f\n", p.x);
        CHECK(p.x > 4.5f && Near(p.y, 0.9f, 0.1f));
        CHECK(CountNamed(scene, "Moeda") == 1);
        const int* pontos = ScriptField<int>(scene, placar, "Placar", "pontos");
        CHECK(pontos && *pontos == 2);
        // A câmera seguiu o jogador (Seguir: deslocamento 0, 4, 7).
        const glm::vec3 c = scene.Find(cameraId)->transform.position;
        CHECK(Near(c.x, p.x, 1.0f) && c.y > 3.0f);
        runtime.Stop();
    }

    SECTION("Porta com trigger: abre com o jogador dentro e fecha quando ele sai");
    {
        Scene scene;
        loadedScenes = {&scene};
        AddFloor(scene);
        AddPlayer(scene, {0.0f, 0.9f, 0.0f});
        AddTrigger(scene, "Sensor", {3.0f, 1.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, "Porta");
        Entity& door = scene.CreateEntity("Porta");
        door.transform.position = {3.0f, 1.0f, 4.0f};
        door.transform.scale = {2.0f, 2.0f, 0.2f};
        door.Add<ColliderComponent>();
        door.Add<RigidbodyComponent>().isKinematic = true;
        const EntityId doorId = door.id;

        SceneRuntime runtime(scene);
        runtime.Start();
        Run(runtime, 0.3f);
        Hold(Key::D, true);
        Run(runtime, 0.75f);  // chega ao sensor (x = 2 a 4)
        Hold(Key::D, false);
        Run(runtime, 1.0f);
        const float open = scene.Find(doorId)->transform.position.y;
        std::printf("  porta aberta em y = %.2f\n", open);
        CHECK(open > 2.5f);
        Hold(Key::D, true);
        Run(runtime, 1.0f);  // sai do sensor
        Hold(Key::D, false);
        Run(runtime, 2.5f);
        CHECK(Near(scene.Find(doorId)->transform.position.y, 1.0f, 0.01f));
        runtime.Stop();
    }

    SECTION("Inimigo: persegue o jogador que vê e o manda de volta ao recomeço");
    {
        Scene scene;
        loadedScenes = {&scene};
        AddFloor(scene);
        const EntityId player = AddPlayer(scene, {0.0f, 0.9f, 0.0f}, false);
        Entity& enemy = scene.CreateEntity("Inimigo");
        enemy.transform.position = {0.0f, 0.9f, -6.0f};
        enemy.Add<CharacterControllerComponent>();
        AddScript(enemy, "Inimigo");
        const EntityId enemyId = enemy.id;
        if (glm::vec3* r = ScriptField<glm::vec3>(scene, enemyId, "Inimigo", "recomeco")) *r = {6.0f, 0.9f, 6.0f};

        SceneRuntime runtime(scene);
        runtime.Start();
        Run(runtime, 1.0f);
        const float before = scene.Find(enemyId)->transform.position.z;
        std::printf("  inimigo em z = %.2f depois de 1 s\n", before);
        CHECK(before > -4.5f);  // andou na direção do jogador
        Run(runtime, 2.0f);
        const glm::vec3 p = scene.Find(player)->transform.position;
        std::printf("  jogador em (%.2f, %.2f, %.2f)\n", p.x, p.y, p.z);
        CHECK(p.x > 4.0f && p.z > 4.0f);  // foi pego e voltou ao recomeço
        runtime.Stop();
    }

    SECTION("Disparador e Projetil: F cria um projétil (do zero ou de um prefab), que some sozinho");
    {
        Scene scene;
        loadedScenes = {&scene};
        Entity& gun = scene.CreateEntity("Arma");
        gun.Add<MeshRenderer>().material.color = {1.0f, 0.0f, 0.0f, 1.0f};
        AddScript(gun, "Disparador");
        const EntityId gunId = gun.id;

        SceneRuntime runtime(scene);
        runtime.SetAssets(&assets);
        runtime.Start();
        Hold(Key::F, true);
        Run(runtime, 0.1f);  // segurar F atira uma vez só (WasKeyPressed)
        Hold(Key::F, false);
        CHECK(CountNamed(scene, "Projétil") == 1);
        const Entity* shot = scene.FindByName("Projétil");
        CHECK(shot && shot->Has<MeshRenderer>() && shot->Get<MeshRenderer>()->material.color.r == 1.0f);
        CHECK(shot && shot->transform.position.z < -1.0f);  // andou para a frente (-Z)
        Run(runtime, 2.5f);
        CHECK(CountNamed(scene, "Projétil") == 0);

        // Com prefab configurado no campo PrefabRef, Instantiate cria a partir do .mprefab.
        assets.SetRoot(root);
        const fs::path prefabFile = root / "Assets" / "Prefabs" / "Tiro.mprefab";
        fs::create_directories(prefabFile.parent_path());
        Scene prefabScene;
        Entity& tmpl = prefabScene.CreateEntity("TiroPrefab");
        tmpl.Add<MeshRenderer>().material.color = {0.0f, 1.0f, 0.0f, 1.0f};
        PrefabLibrary prefabLib;
        CHECK(Prefabs::CreateFromEntity(prefabScene, tmpl.id, prefabFile, prefabLib, assets) != nullptr);
        if (PrefabRef* ref = ScriptField<PrefabRef>(scene, gunId, "Disparador", "prefab")) {
            ref->guid = assets.Database().GuidFor(prefabFile);
            ref->path = "Assets/Prefabs/Tiro.mprefab";
            ref->file = prefabFile;
        }
        Hold(Key::F, true);
        Run(runtime, 0.1f);
        Hold(Key::F, false);
        const Entity* fromPrefab = scene.FindByName("TiroPrefab");
        CHECK(fromPrefab && fromPrefab->Has<MeshRenderer>() && fromPrefab->Get<MeshRenderer>()->material.color.g == 1.0f);
        CHECK(fromPrefab && fromPrefab->transform.position.z < -1.0f);
        runtime.Stop();
    }

    SECTION("Bola: força com as setas e impulso no chão");
    {
        Scene scene;
        loadedScenes = {&scene};
        AddFloor(scene);
        Entity& ball = scene.CreateEntity("Bola");
        ball.transform.position = {0.0f, 0.5f, 0.0f};
        ball.Add<ColliderComponent>().shape = ColliderShape::Sphere;
        ball.Add<RigidbodyComponent>();
        AddScript(ball, "BolaFisica");
        const EntityId ballId = ball.id;

        SceneRuntime runtime(scene);
        runtime.Start();
        Run(runtime, 0.3f);
        Hold(Key::Right, true);
        Run(runtime, 0.4f);
        Hold(Key::Right, false);
        std::printf("  bola rolou até x = %.2f\n", scene.Find(ballId)->transform.position.x);
        CHECK(scene.Find(ballId)->transform.position.x > 0.5f);
        Hold(Key::Space, true);
        float highest = 0.0f;
        for (int i = 0; i < 30; ++i) {
            Run(runtime, kDt);
            Hold(Key::Space, false);
            highest = std::max(highest, scene.Find(ballId)->transform.position.y);
        }
        std::printf("  bola subiu até y = %.2f\n", highest);
        CHECK(highest > 1.0f);
        runtime.Stop();
    }

    SECTION("Primeiros exemplos: Flutuar, campos e ciclo de vida");
    {
        Scene scene;
        loadedScenes = {&scene};
        Entity& buoy = scene.CreateEntity("Boia");
        AddScript(buoy, "Flutuar");
        AddScript(buoy, "CicloDeVida");
        const EntityId buoyId = buoy.id;
        Entity& fields = scene.CreateEntity("Campos");
        fields.Add<MeshRenderer>();
        AddScript(fields, "ExemploCampos");
        const EntityId fieldsId = fields.id;

        SceneRuntime runtime(scene);
        runtime.Start();
        Run(runtime, 0.5f);
        CHECK(std::abs(scene.Find(buoyId)->transform.position.y) > 0.1f);
        const glm::vec4 color = scene.Find(fieldsId)->Get<MeshRenderer>()->material.color;
        CHECK(Near(color.g, 0.8f, 1e-4f));  // a cor do script foi para o Mesh Renderer
        const int* passos = ScriptField<int>(scene, buoyId, "CicloDeVida", "passosFixos");
        CHECK(passos == nullptr);  // não registrado: estado interno
        runtime.Stop();
    }

    SECTION("Interruptor e lâmpada: EntityRef, mensagem e temporizador");
    {
        Scene scene;
        loadedScenes = {&scene};
        AddFloor(scene);
        Entity& lamp = scene.CreateEntity("Lampada");
        lamp.transform.position = {5.0f, 3.0f, 0.0f};
        lamp.Add<MeshRenderer>().material.color = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
        AddScript(lamp, "Lampada");
        const EntityId lampId = lamp.id;
        const EntityId switchId = AddTrigger(scene, "Interruptor", {0.0f, 1.0f, 0.0f}, {3.0f, 3.0f, 3.0f}, "Interruptor");
        if (auto* ref = ScriptField<EntityRef>(scene, switchId, "Interruptor", "lampada")) ref->id = lampId;
        if (auto* duracao = ScriptField<float>(scene, lampId, "Lampada", "duracao")) *duracao = 0.5f;
        AddPlayer(scene, {0.0f, 1.0f, 0.0f}, false);  // já dentro da área do interruptor

        SceneRuntime runtime(scene);
        runtime.Start();
        Run(runtime, 0.2f);
        const glm::vec4 on{1.0f, 0.9f, 0.4f, 1.0f};
        CHECK(scene.Find(lampId)->Get<MeshRenderer>()->material.color == on);
        Run(runtime, 0.6f);  // depois da duração: apaga sozinha
        CHECK(scene.Find(lampId)->Get<MeshRenderer>()->material.color == glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
        CHECK(runtime.TimerCount() == 0);
        runtime.Stop();
    }

    SECTION("Chuva: Every cria gotas em lugares sorteados e After as destrói");
    {
        Scene scene;
        loadedScenes = {&scene};
        Entity& cloud = scene.CreateEntity("Nuvem");
        cloud.transform.position = {10.0f, 5.0f, -2.0f};
        AddScript(cloud, "Chuva");
        const EntityId cloudId = cloud.id;
        SceneRuntime runtime(scene);
        runtime.Start();
        Run(runtime, 1.0f);
        CHECK(CountNamed(scene, "Gota") >= 3 && CountNamed(scene, "Gota") <= 4);
        Run(runtime, 2.0f);
        const int* criadas = ScriptField<int>(scene, cloudId, "Chuva", "criadas");
        CHECK(criadas == nullptr);  // estado do jogo: não registrado
        const int alive = CountNamed(scene, "Gota");
        CHECK(alive >= 5 && alive <= 7);  // 1,5 s de vida a cada 0,25 s
        bool inside = true;
        for (const auto& e : scene.Entities()) {
            if (e->name != "Gota") continue;
            const glm::vec3 p = e->transform.position;
            inside = inside && std::abs(p.x - 10.0f) <= 4.0f && std::abs(p.z + 2.0f) <= 4.0f && p.y == 5.0f;
        }
        CHECK(inside);
        runtime.Stop();
    }

    SECTION("HUD e menu do guia: vida e pontos na tela, Jogar troca de cena");
    {
        const glm::vec2 screen{1920.0f, 1080.0f};
        auto click = [&](SceneRuntime& runtime, glm::vec2 at) {
            UiSystem* ui = runtime.GetSystem<UiSystem>();
            for (bool down : {true, false, false}) {  // aperta, solta (clique) e o script lê no frame seguinte
                if (ui) ui->SetPointer({screen, at, true, down});
                runtime.Update(kDt);
            }
        };

        Scene scene;
        loadedScenes = {&scene};
        const EntityId player = scene.CreateEntity("Jogador").id;
        Entity& placarEntity = scene.CreateEntity("Placar");
        AddScript(placarEntity, "Placar");
        const EntityId placar = placarEntity.id;
        for (const char* name : {"Vida", "Pontos"}) {
            Entity& e = scene.CreateEntity(name);
            e.Add<UiRect>();
            e.Add<UiText>();
        }
        scene.CreateEntity("FimDeJogo").Add<UiImage>();
        AddScript(scene.CreateEntity("HUD"), "Hud");

        SceneRuntime runtime(scene);
        runtime.Start();
        Run(runtime, 0.1f);
        UiSystem* ui = runtime.GetSystem<UiSystem>();
        CHECK(ui && ui->GetText("Vida") == "Vida: 3" && ui->GetText("Pontos") == "Pontos: 0");
        CHECK(ui && !ui->IsVisible("FimDeJogo"));
        if (int* pontos = ScriptField<int>(scene, placar, "Placar", "pontos")) *pontos = 4;
        Run(runtime, 0.1f);
        CHECK(ui && ui->GetText("Pontos") == "Pontos: 4");
        for (int i = 0; i < 3; ++i) {  // cai do chão três vezes: volta ao começo e perde uma vida
            scene.Find(player)->transform.position.y = -10.0f;
            Run(runtime, 0.1f);
            CHECK(scene.Find(player)->transform.position.y == 0.0f);
        }
        CHECK(ui && ui->GetText("Vida") == "Vida: 0" && ui->IsVisible("FimDeJogo"));
        runtime.Stop();

        Scene menu;
        loadedScenes = {&menu};
        Entity& jogar = menu.CreateEntity("Jogar");  // UiRect padrão: centro da tela, 200 x 60
        jogar.Add<UiRect>();
        jogar.Add<UiButton>();
        Entity& creditos = menu.CreateEntity("Creditos");
        creditos.Add<UiRect>().position = {0.0f, 100.0f};
        creditos.Add<UiButton>();
        menu.CreateEntity("PainelCreditos").Add<UiRect>().position = {0.0f, -300.0f};
        menu.FindByName("PainelCreditos")->Add<UiImage>();
        AddScript(menu.CreateEntity("Menu"), "MenuPrincipal");

        SceneRuntime menuRuntime(menu);
        menuRuntime.Start();
        UiSystem* menuUi = menuRuntime.GetSystem<UiSystem>();
        CHECK(menuUi && !menuUi->IsVisible("PainelCreditos"));
        click(menuRuntime, {960.0f, 640.0f});
        CHECK(menuUi && menuUi->IsVisible("PainelCreditos") && !menuRuntime.HasSceneRequest());
        click(menuRuntime, {960.0f, 540.0f});
        CHECK(menuRuntime.HasSceneRequest() && menuRuntime.TakeSceneRequest() == "Fase1");
        menuRuntime.Stop();
    }

    SECTION("Sons do menu: clique do botão toca o som; M liga e desliga a música (sem dispositivo de áudio)");
    {
        const fs::path wav = root / "Assets" / "Sons" / "clique.wav";
        fs::create_directories(wav.parent_path());
        {
            // WAV PCM 16 bits mono, 0,1 s de um tom quadrado.
            std::ofstream out(wav, std::ios::binary);
            auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
            auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
            const uint32_t frames = 4800;
            out.write("RIFF", 4);
            u32(36 + frames * 2);
            out.write("WAVEfmt ", 8);
            u32(16), u16(1), u16(1), u32(48000), u32(96000), u16(2), u16(16);
            out.write("data", 4);
            u32(frames * 2);
            for (uint32_t i = 0; i < frames; ++i) u16(uint16_t((i / 50) % 2 ? 8000 : -8000));
        }
        AudioClip clip;
        clip.path = "Assets/Sons/clique.wav";
        clip.file = wav;
        AudioSettings::Global().useDevice = false;
        const glm::vec2 screen{1920.0f, 1080.0f};

        Scene scene;
        loadedScenes = {&scene};
        Entity& jogar = scene.CreateEntity("Jogar");
        jogar.Add<UiRect>();
        jogar.Add<UiButton>();
        for (const char* name : {"SomClique", "Musica"}) {
            AudioSource& source = scene.CreateEntity(name).Add<AudioSource>();
            source.clip = clip;
            source.spatial = false;
            source.playOnStart = std::string(name) == "Musica";
            source.loop = source.playOnStart;
        }
        AddScript(scene.CreateEntity("Sons"), "SonsDoMenu");

        SceneRuntime runtime(scene);
        runtime.Start();
        AudioSystem* audio = runtime.GetSystem<AudioSystem>();
        CHECK(audio && audio->IsPlaying("Musica") && !audio->IsPlaying("SomClique"));
        CHECK(scene.FindByName("Musica")->Get<AudioSource>()->volume == 0.5f);  // volumeDaMusica no OnStart
        UiSystem* ui = runtime.GetSystem<UiSystem>();
        for (bool down : {true, false, false}) {  // clique no centro da tela (o botão)
            if (ui) ui->SetPointer({screen, {960.0f, 540.0f}, true, down});
            runtime.Update(kDt);
        }
        CHECK(audio && audio->IsPlaying("SomClique"));
        Hold(Key::M, true);
        Run(runtime, kDt);
        Hold(Key::M, false);
        Run(runtime, kDt);
        CHECK(audio && !audio->IsPlaying("Musica"));
        Hold(Key::M, true);
        Run(runtime, kDt);
        Hold(Key::M, false);
        CHECK(audio && audio->IsPlaying("Musica"));
        runtime.Stop();
        CHECK(runtime.GetSystem<AudioSystem>() == nullptr);
    }

    SECTION("Guarda: patrulha entre os pontos pelo navmesh, persegue o alvo que vê e volta a patrulhar");
    {
        Scene scene;
        loadedScenes = {&scene};
        AddFloor(scene);
        Entity& wall = scene.CreateEntity("Parede");  // de x = -6 a 6 em z = 0: a patrulha dá a volta por x = 8
        wall.transform.position = {0.0f, 1.5f, 0.0f};
        wall.Add<ColliderComponent>().size = {12.0f, 3.0f, 1.0f};
        scene.CreateEntity("Navegação").Add<NavMeshSurface>();
        const glm::vec3 a{8.0f, 0.0f, -6.0f}, b{8.0f, 0.0f, 6.0f};
        Entity& pointA = scene.CreateEntity("PontoA");
        pointA.transform.position = a;
        const EntityId pointAId = pointA.id;
        Entity& pointB = scene.CreateEntity("PontoB");
        pointB.transform.position = b;
        const EntityId pointBId = pointB.id;
        Entity& target = scene.CreateEntity("Alvo");
        target.transform.position = {-8.0f, 0.0f, 8.0f};  // longe e atrás da parede
        const EntityId targetId = target.id;
        Entity& guard = scene.CreateEntity("Guarda");
        guard.transform.position = {6.0f, 0.0f, -6.0f};
        guard.Add<NavMeshAgent>();
        AddScript(guard, "Guarda");
        const EntityId guardId = guard.id;
        if (auto* ref = ScriptField<EntityRef>(scene, guardId, "Guarda", "pontoA")) ref->id = pointAId;
        if (auto* ref = ScriptField<EntityRef>(scene, guardId, "Guarda", "pontoB")) ref->id = pointBId;
        if (auto* ref = ScriptField<EntityRef>(scene, guardId, "Guarda", "alvo")) ref->id = targetId;

        SceneRuntime runtime(scene);
        runtime.Start();
        NavSystem* nav = runtime.GetSystem<NavSystem>();
        CHECK(nav && nav->GetNavMesh());
        auto where = [&] { return scene.Find(guardId)->transform.position; };

        // Patrulha: vai até B e depois volta até A.
        float nearB = 1e9f, nearAAfterB = 1e9f;
        for (int i = 0; i < 120; ++i) {
            Run(runtime, 0.1f);
            nearB = std::min(nearB, glm::distance(where(), b));
            if (nearB < 0.3f) nearAAfterB = std::min(nearAAfterB, glm::distance(where(), a));
        }
        std::printf("  patrulha: chegou a %.2f m de B e depois a %.2f m de A\n", nearB, nearAAfterB);
        CHECK(nearB < 0.3f && nearAAfterB < 0.3f);

        // O alvo aparece a 3 m, à vista: o guarda vai até ele.
        for (int i = 0; i < 60 && std::abs(where().z) < 3.0f; ++i) Run(runtime, 0.1f);  // longe da parede
        scene.Find(targetId)->transform.position = where() + glm::vec3(-3.0f, 0.0f, 0.0f);
        Run(runtime, 1.5f);
        const float chase = glm::distance(where(), scene.Find(targetId)->transform.position);
        std::printf("  perseguição: a %.2f m do alvo\n", chase);
        CHECK(chase < 0.5f);

        // O alvo some atrás da parede: o guarda volta para um dos pontos da patrulha.
        scene.Find(targetId)->transform.position = {-8.0f, 0.0f, 8.0f};
        Run(runtime, 0.2f);
        const std::vector<glm::vec3> path = nav ? nav->GetPath(guardId) : std::vector<glm::vec3>{};
        CHECK(!path.empty());
        CHECK(!path.empty() && std::min(glm::distance(path.back(), a), glm::distance(path.back(), b)) < 0.3f);
        runtime.Stop();
    }

    SECTION("Pastor: o rebanho vai ao curral desviando entre si; com chuva dá a volta na lama, sem chuva atravessa");
    for (const bool rain : {false, true}) {
        // Faixa de lama (x de -2 a 2, z de -10 a 6) entre o rebanho e o curral; a volta pela ponta livre tem uns 25 m.
        Scene scene;
        loadedScenes = {&scene};
        AddFloor(scene);
        scene.CreateEntity("Navegação").Add<NavMeshSurface>();
        Entity& mud = scene.CreateEntity("Lama");
        mud.transform.position = {0.0f, 0.0f, -2.0f};
        NavMeshArea& area = mud.Add<NavMeshArea>();
        area.size = {4.0f, 2.0f, 16.0f};
        area.area = 2;
        area.name = "Lama";
        area.cost = 1.5f;
        const EntityId mudId = mud.id;
        Entity& pen = scene.CreateEntity("Curral");
        pen.transform.position = {7.0f, 0.0f, -5.0f};
        const EntityId penId = pen.id;
        Entity& shepherd = scene.CreateEntity("Pastor");
        shepherd.transform.position = {-7.0f, 0.0f, -5.0f};
        AddScript(shepherd, "Pastor");
        const EntityId shepherdId = shepherd.id;
        std::vector<EntityId> sheep;
        for (int i = 0; i < 6; ++i) {
            Entity& s = scene.CreateEntity("Ovelha", shepherdId);
            s.transform.position = {-1.0f + (i % 2) * 2.0f, 0.0f, -2.0f + (i / 2) * 2.0f};
            s.Add<NavMeshAgent>();
            sheep.push_back(s.id);
        }
        if (auto* ref = ScriptField<EntityRef>(scene, shepherdId, "Pastor", "curral")) ref->id = penId;
        if (auto* ref = ScriptField<EntityRef>(scene, shepherdId, "Pastor", "lama")) ref->id = mudId;
        if (bool* field = ScriptField<bool>(scene, shepherdId, "Pastor", "chovendo")) *field = rain;

        SceneRuntime runtime(scene);
        runtime.Start();
        NavSystem* nav = runtime.GetSystem<NavSystem>();
        CHECK(nav && nav->GetNavMesh());
        bool inMud = false;
        float farthestZ = -1e9f;  // a volta passa por z > 6; o caminho reto fica em z < 0
        float closest = 1e9f;
        int arrived = 0;
        for (int step = 0; step < 300 && arrived < 6; ++step) {
            Run(runtime, 0.1f);
            arrived = 0;
            for (size_t i = 0; i < sheep.size(); ++i) {
                const glm::vec3 p = glm::vec3(scene.WorldMatrix(sheep[i])[3]);
                if (std::abs(p.x) < 1.9f && p.z < 5.9f) inMud = true;
                farthestZ = std::max(farthestZ, p.z);
                for (size_t j = i + 1; j < sheep.size(); ++j) {
                    closest = std::min(closest, glm::distance(p, glm::vec3(scene.WorldMatrix(sheep[j])[3])));
                }
                if (nav && nav->HasArrived(sheep[i])) ++arrived;
            }
        }
        std::printf("  %s: %d ovelhas no curral, %s, a %.2f m uma da outra no mais perto\n", rain ? "com chuva" : "sem chuva",
                    arrived, inMud ? "atravessaram a lama" : "deram a volta na lama", closest);
        CHECK(arrived == 6);
        CHECK(inMud == !rain);
        CHECK(rain ? farthestZ > 5.5f : farthestZ < 0.0f);
        CHECK(closest > 0.6f);
        CHECK(Near(scene.Find(mudId)->Get<NavMeshArea>()->cost, rain ? 6.0f : 1.5f, 1e-4f));
        for (EntityId s : sheep) CHECK(glm::distance(glm::vec3(scene.WorldMatrix(s)[3]), {7.0f, 0.0f, -5.0f}) < 3.0f);
        runtime.Stop();
    }

    loadedScenes.clear();
    host.Unload(scenes, assets);
    CHECK(!host.IsLoaded());
    return TestResult();
}
