// Scripts por projeto: CMake gerado, nomes, modelos, diagnósticos do compilador e, de ponta a ponta,
// compilar a pasta Scripts/ de um projeto numa DLL com o toolchain do Motor, carregar, rodar, recarregar
// preservando os campos e descarregar. Sem janela nem OpenGL.
#include "TestUtil.h"

#include "ScriptBuilder.h"
#include "ScriptHost.h"
#include "ScriptProject.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/core/Paths.h>
#include <motor/scene/Component.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/scene/SceneSerializer.h>

#include <glm/gtc/quaternion.hpp>

#include <chrono>
#include <cstdio>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

using namespace motor;
using namespace editor;
namespace fs = std::filesystem;
namespace SP = editor::ScriptProject;

namespace {
bool Contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

std::string ReadText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void WriteText(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

// Compila e espera. Mostra a saída se falhar (ajuda a entender o problema no log do ctest).
bool BuildAndWait(ScriptBuilder& builder, const SP::Paths& paths, const SP::Toolchain& toolchain, bool expectOk = true) {
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
    if (ok != expectOk) std::printf("----- saída da compilação -----\n%s\n-------------------------------\n", builder.Log().c_str());
    const ScriptBuilder::Timings t = builder.GetTimings();
    std::string filesStr;
    for (const auto& f : t.files) filesStr += std::format("{}{}: {:.2f} s", filesStr.empty() ? "" : ", ", f.file, f.seconds);
    std::printf("  (compilação: %.2f s | config %.2f s, compilar %.2f s [%s], ligar %.2f s)\n",
                builder.Seconds(), t.configure, t.compile, filesStr.c_str(), t.link);
    return ok;
}

ComponentPtr MissingGirar(float velocidade) {
    ComponentPtr c(ComponentRegistry::MissingType());
    auto& m = *static_cast<MissingComponent*>(c.Data());
    m.type = "Girar";
    m.json = std::format(R"({{"type": "Girar", "velocidade": {}}})", velocidade);
    return c;
}

int RunBench(const fs::path& baseDir) {
    std::string err;
    const auto toolchain = SP::LoadToolchain(MOTOR_TEST_SDK_JSON, &err);
    if (!toolchain) {
        std::printf("Erro no SDK: %s\n", err.c_str());
        return 1;
    }
    std::printf("| Caso | Configurar | Compilar | Ligar | Recarregar | Total |\n");
    std::printf("|---|---|---|---|---|---|\n");
    for (int count : {1, 10, 50}) {
        const fs::path projDir = baseDir / std::format("Bench_{}", count);
        std::error_code ec;
        fs::remove_all(projDir, ec);
        const SP::Paths paths = SP::PathsFor(projDir);
        fs::create_directories(paths.scriptsDir, ec);
        for (int i = 1; i <= count; ++i) {
            const std::string name = std::format("BenchScript_{:02d}", i);
            WriteText(paths.scriptsDir / (name + ".cpp"), SP::NewScriptSource(name));
        }
        ScriptBuilder builder;
        ScriptHost host;
        AssetLibrary assets;
        Scene scene;
        auto scenes = [&](const std::function<void(Scene&)>& fn) { fn(scene); };
        if (!BuildAndWait(builder, paths, *toolchain)) return 1;
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = host.Load(paths.dll, paths.loadDir, scenes, assets);
        const float reloadSec = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
        if (!r.ok) return 1;
        const auto t = builder.GetTimings();
        std::printf("| %d script(s) (do zero) | %.2f s | %.2f s | %.2f s | %.3f s | %.2f s |\n",
                    count, t.configure, t.compile, t.link, reloadSec, t.total + reloadSec);
        if (count == 10) {
            // Mudar 1 arquivo num projeto de 10 scripts:
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));
            const fs::path one = paths.scriptsDir / "BenchScript_01.cpp";
            WriteText(one, ReadText(one) + "\n// mudanca incremental\n");
            if (!BuildAndWait(builder, paths, *toolchain)) return 1;
            const auto t1 = std::chrono::steady_clock::now();
            host.Load(paths.dll, paths.loadDir, scenes, assets);
            const float reloadInc = std::chrono::duration<float>(std::chrono::steady_clock::now() - t1).count();
            const auto ti = builder.GetTimings();
            std::printf("| 10 scripts (1 arquivo editado) | %.2f s | %.2f s | %.2f s | %.3f s | %.2f s |\n",
                        ti.configure, ti.compile, ti.link, reloadInc, ti.total + reloadInc);
        }
        host.Unload(scenes, assets);
        fs::remove_all(projDir, ec);
    }
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--bench") {
            return RunBench(fs::temp_directory_path() / "motor_bench_scripts");
        }
    }
    const fs::path dir = TestDir(argc, argv, "tmp_test_script_project");
    const fs::path root = dir / PathFromUtf8("Jogo Ação");  // acentos no caminho: caso de teste padrão
    const SP::Paths paths = SP::PathsFor(root);

    SECTION("Caminhos e arquivos gerados");
    CHECK(paths.scriptsDir == root / "Scripts");
    CHECK(paths.dll.filename() == "GameScripts.dll" && IsInside(paths.dll, root / ".motor"));
    SP::Toolchain fake;
    fake.cmake = "C:/CMake/bin/cmake.exe";
    fake.generator = "MSYS Makefiles";
    fake.makeProgram = "C:/msys64/usr/bin/make.exe";
    fake.cxxCompiler = "C:/msys64/ucrt64/bin/c++.exe";
    fake.buildType = "Debug";
    fake.sdkCMake = PathFromUtf8("C:/Motor/build/bin/sdk/MotorSDK.cmake");
    {
        const std::string cmake = SP::GenerateCMakeLists(paths, fake);
        CHECK(Contains(cmake, "include(\"C:/Motor/build/bin/sdk/MotorSDK.cmake\")"));
        CHECK(Contains(cmake, "add_library(GameScripts SHARED"));
        CHECK(Contains(cmake, "target_link_libraries(GameScripts PRIVATE Motor::Motor)"));
        CHECK(Contains(cmake, "CONFIGURE_DEPENDS \"" + ToGenericUtf8(paths.scriptsDir) + "/*.cpp\""));  // UTF-8, barras '/'
        CHECK(Contains(cmake, "Jogo Ação/Scripts"));
        CHECK(!Contains(cmake, "\\"));  // nenhuma barra invertida
        // Objetos com nome curto, sem acentos (o as.exe não lê UTF-8) e abaixo de 260 caracteres.
        CHECK(Contains(cmake, "set(CMAKE_INTERMEDIATE_DIR_STRATEGY SHORT CACHE STRING \"\")"));
        CHECK(Contains(cmake, "set(CMAKE_OBJECT_PATH_MAX 240)"));
        const std::string entry = SP::GenerateEntryPoint();
        CHECK(Contains(entry, "MotorRegisterScripts") && Contains(entry, "MotorScriptApiVersion"));
        CHECK(Contains(entry, "__declspec(dllexport)"));

        const auto configure = SP::ConfigureCommand(paths, fake);
        CHECK(configure.size() >= 7 && configure[0] == ToUtf8(fake.cmake) && configure[1] == "-S");
        CHECK(Contains(configure.back(), "CMAKE_MAKE_PROGRAM=C:/msys64/usr/bin/make.exe"));
        const auto build = SP::BuildCommand(paths, fake);
        CHECK(build.size() >= 3 && build[1] == "--build");
        CHECK(SP::ToolchainPathDirs(fake).size() == 2);
        CHECK(SP::NeedsConfigure(paths));
        // Configuração interrompida: cache sem Makefile ainda precisa configurar (e o cache é apagado antes).
        fs::create_directories(paths.binaryDir / "CMakeFiles");
        { std::ofstream(paths.binaryDir / "CMakeCache.txt") << "CMAKE_GENERATOR:INTERNAL=MSYS Makefiles\n"; }
        CHECK(SP::NeedsConfigure(paths));
        { std::ofstream(paths.binaryDir / "Makefile") << "all:\n"; }
        CHECK(!SP::NeedsConfigure(paths));
        fs::remove(paths.binaryDir / "Makefile");
        SP::ResetConfigure(paths);
        CHECK(!fs::exists(paths.binaryDir / "CMakeCache.txt") && !fs::exists(paths.binaryDir / "CMakeFiles"));

        // Caminho longo demais para o compilador (limite de 260 do Windows): aviso com a provável causa.
        CHECK(SP::LongPathWarning(SP::PathsFor("C:/Jogos/Meu Jogo")).empty());
        const fs::path deep = fs::path("C:/") / std::string(150, 'p') / "Projeto Robô";
        const std::string warning = SP::LongPathWarning(SP::PathsFor(deep));
        CHECK(Contains(warning, "longo demais") && Contains(warning, "Projeto Robô"));

        // Gravar duas vezes o mesmo conteúdo não muda a data (o CMake não reconfigura à toa).
        CHECK(SP::WriteBuildFiles(paths, fake));
        const auto t1 = fs::last_write_time(paths.cmakeDir / "CMakeLists.txt");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK(SP::WriteBuildFiles(paths, fake));
        CHECK(fs::last_write_time(paths.cmakeDir / "CMakeLists.txt") == t1);
        fs::remove_all(root / ".motor");
    }

    SECTION("Nomes de script, esqueleto e modelos");
    CHECK(SP::ValidateScriptName("Girar").empty());
    CHECK(SP::ValidateScriptName("Mover_2").empty());
    CHECK(!SP::ValidateScriptName("").empty());
    CHECK(!SP::ValidateScriptName("2Girar").empty());
    CHECK(!SP::ValidateScriptName("Ação").empty());
    CHECK(!SP::ValidateScriptName("Meu Script").empty());
    CHECK(Contains(SP::NewScriptSource("Pular"), "struct Pular : motor::Script"));
    CHECK(Contains(SP::NewScriptSource("Pular"), "MOTOR_SCRIPT(Pular)"));
    {
        fs::path created;
        CHECK(SP::CreateScript(paths.scriptsDir, "Pular", &created));
        CHECK(created == paths.scriptsDir / "Pular.cpp" && fs::is_regular_file(created));
        std::string error;
        CHECK(!SP::CreateScript(paths.scriptsDir, "Pular", nullptr, &error) && !error.empty());  // já existe

        const fs::path templates = MOTOR_TEST_TEMPLATES_DIR;
        const auto names = SP::ListTemplates(templates);
        CHECK(names == (std::vector<std::string>{"Girar", "Jogador", "MoverWASD", "Seguir"}));
        CHECK(SP::CopyTemplate(templates, "Girar", paths.scriptsDir));
        CHECK(!SP::CopyTemplate(templates, "Girar", paths.scriptsDir));  // não sobrescreve
        CHECK(!SP::CopyTemplate(templates, "NaoExiste", paths.scriptsDir));

        const auto files = SP::ListScriptFiles(paths.scriptsDir);
        CHECK(files.size() == 2 && files[0].filename() == "Girar.cpp" && files[1].filename() == "Pular.cpp");
        const std::string sig = SP::Signature(paths.scriptsDir);
        CHECK(sig == SP::Signature(paths.scriptsDir));
        WriteText(paths.scriptsDir / "Pular.cpp", ReadText(paths.scriptsDir / "Pular.cpp") + "// editado\n");
        CHECK(sig != SP::Signature(paths.scriptsDir));  // arquivo salvo
        WriteText(paths.scriptsDir / "notas.txt", "não é script");
        const std::string sig2 = SP::Signature(paths.scriptsDir);
        fs::remove(paths.scriptsDir / "notas.txt");
        CHECK(sig2 == SP::Signature(paths.scriptsDir));  // outros arquivos não contam
        fs::remove(paths.scriptsDir / "Pular.cpp");
    }

    SECTION("Erros do compilador, do linker e do CMake");
    {
        const std::string log =
            "[ 50%] Building CXX object CMakeFiles/GameScripts.dir/C_/Jogo/Scripts/Girar.cpp.obj\n"
            "C:/Jogo Ação/Scripts/Girar.cpp:12:5: error: 'foo' was not declared in this scope\n"
            "   12 |     foo();\n"
            "C:/Jogo/Scripts/Girar.cpp:3:10: fatal error: nada.h: No such file or directory\r\n"
            "C:/Jogo/Scripts/Mover.cpp:20:9: warning: unused variable 'x' [-Wunused-variable]\n"
            "C:/Jogo/Scripts/Mover.cpp:20:9: note: declared here\n"
            "C:/msys64/ucrt64/bin/../lib/gcc/ld.exe: Girar.cpp.obj: undefined reference to `Pular()'\n"
            "collect2.exe: error: ld returned 1 exit status\n"
            "CMake Error at CMakeLists.txt:5 (include):\n";
        const auto d = SP::ParseDiagnostics(log);
        CHECK(d.size() == 6);
        if (d.size() == 6) {
            CHECK(d[0].kind == SP::Diagnostic::Kind::Error && d[0].file == "C:/Jogo Ação/Scripts/Girar.cpp");
            CHECK(d[0].line == 12 && d[0].column == 5 && d[0].message == "'foo' was not declared in this scope");
            CHECK(d[1].kind == SP::Diagnostic::Kind::Error && d[1].line == 3 && d[1].message == "nada.h: No such file or directory");
            CHECK(d[2].kind == SP::Diagnostic::Kind::Warning && d[2].file == "C:/Jogo/Scripts/Mover.cpp");
            CHECK(d[3].file.empty() && Contains(d[3].message, "undefined reference"));
            CHECK(d[4].file.empty() && Contains(d[5].message, "CMake Error"));
        }
    }

    SECTION("De ponta a ponta: compilar, carregar, rodar, recarregar e descarregar a DLL de scripts");
    std::string toolchainError;
    const auto toolchain = SP::LoadToolchain(MOTOR_TEST_SDK_JSON, &toolchainError);
    CHECK(toolchain.has_value());
    if (!toolchain) {
        std::printf("  %s\n", toolchainError.c_str());
        return TestResult();
    }

    AssetLibrary assets;
    Scene scene;
    Entity& cubo = scene.CreateEntity("Cubo");
    cubo.components.push_back(MissingGirar(45.0f));  // como na cena de um projeto novo, antes de compilar
    const EntityId id = cubo.id;
    auto scenes = [&](const std::function<void(Scene&)>& fn) { fn(scene); };

    ScriptBuilder builder;
    ScriptHost host;
    CHECK(BuildAndWait(builder, paths, *toolchain));
    CHECK(fs::is_regular_file(paths.dll));
    CHECK(SP::ParseDiagnostics(builder.Log()).empty());  // os modelos compilam sem avisos
    {
        const ScriptBuilder::Timings t = builder.GetTimings();
        CHECK(t.configure > 0.0f && t.compile > 0.0f && t.link > 0.0f && t.total > 0.0f);
        bool foundGirar = false;
        for (const auto& f : t.files) {
            if (f.file == "Girar.cpp" && f.seconds > 0.0f) foundGirar = true;
        }
        CHECK(foundGirar);
    }

    ScriptHost::Result r1 = host.Load(paths.dll, paths.loadDir, scenes, assets);
    CHECK(r1.ok);
    if (!r1.ok) std::printf("  %s\n", r1.error.c_str());
    CHECK(r1.resolved == 1 && r1.types == std::vector<std::string>{"Girar"});
    const ComponentType* girar = ComponentRegistry::Instance().Find("Girar");
    CHECK(girar && girar->IsScript());
    CHECK(girar && scene.Find(id)->Get(*girar) != nullptr);
    CHECK(fs::is_regular_file(host.LoadedCopy()) && host.LoadedCopy() != paths.dll);

    // O script da DLL roda no SceneRuntime da engine (mesmo registro, mesmo Transform).
    {
        SceneRuntime runtime(scene);
        runtime.Start();
        for (int i = 0; i < 10; ++i) runtime.Update(0.1f);  // 1 s a 45°/s
        runtime.Stop();
        const glm::vec3 forward = scene.Find(id)->transform.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        const glm::vec3 expected = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0)) * glm::vec3(0.0f, 0.0f, -1.0f);
        CHECK(glm::length(forward - expected) < 1e-3f);
    }

    // Valor editado "no Inspector": tem que sobreviver à recarga.
    if (girar) {
        void* data = scene.Find(id)->Get(*girar);
        for (const FieldInfo& f : girar->fields) {
            if (f.key == "velocidade") f.As<float>(data) = 12.5f;
        }
    }

    SECTION("Recarga com o script alterado e um script novo");
    {
        std::string source = ReadText(paths.scriptsDir / "Girar.cpp");
        const std::string from = "bool local = true;";
        const size_t at = source.find(from);
        CHECK(at != std::string::npos);
        if (at != std::string::npos) source.insert(at + from.size(), "\n    int voltas = 3;");
        const std::string reg = ".Field(\"eixo\", \"Eixo\", &Girar::eixo)";
        const size_t regAt = source.find(reg);
        if (regAt != std::string::npos) source.insert(regAt + reg.size(), "\n        .Field(\"voltas\", \"Voltas\", &Girar::voltas)");
        WriteText(paths.scriptsDir / "Girar.cpp", source);
        CHECK(SP::CreateScript(paths.scriptsDir, "Pular"));
    }
    CHECK(BuildAndWait(builder, paths, *toolchain));  // a DLL carregada é uma cópia: o linker reescreve a original
    ScriptHost::Result r2 = host.Load(paths.dll, paths.loadDir, scenes, assets);
    CHECK(r2.ok && r2.detached == 1 && r2.resolved == 1);
    CHECK((r2.types == std::vector<std::string>{"Girar", "Pular"}));
    girar = ComponentRegistry::Instance().Find("Girar");
    if (girar) {
        const void* data = scene.Find(id)->Get(*girar);
        CHECK(data != nullptr);
        for (const FieldInfo& f : girar->fields) {
            if (data && f.key == "velocidade") CHECK(f.As<float>(data) == 12.5f);  // estado preservado
            if (data && f.key == "voltas") CHECK(f.As<int>(data) == 3);            // campo novo: padrão
        }
        CHECK(girar->fields.size() == 4);
    }
    {
        // Só a cópia em uso fica na pasta de cópias.
        size_t copies = 0;
        std::error_code ec;
        for (fs::directory_iterator it(paths.loadDir, ec), end; !ec && it != end; it.increment(ec)) {
            copies += it->path().extension() == ".dll";
        }
        CHECK(copies == 1);
    }

    SECTION("Erro de compilação: aparece nos diagnósticos e os scripts carregados continuam");
    WriteText(paths.scriptsDir / "Quebrado.cpp", "#include <motor/scene/ScriptModule.h>\nvoid f() { naoExiste(); }\n");
    CHECK(!BuildAndWait(builder, paths, *toolchain, false));
    {
        const auto d = SP::ParseDiagnostics(builder.Log());
        CHECK(!d.empty() && d[0].kind == SP::Diagnostic::Kind::Error && Contains(d[0].file, "Quebrado.cpp") && d[0].line == 2);
        CHECK(host.IsLoaded() && ComponentRegistry::Instance().Find("Girar") != nullptr);
    }
    fs::remove(paths.scriptsDir / "Quebrado.cpp");

    SECTION("Descarregar: o componente volta a ser desconhecido, com os dados");
    host.Unload(scenes, assets);
    CHECK(!host.IsLoaded());
    CHECK(ComponentRegistry::Instance().Find("Girar") == nullptr && ComponentRegistry::Instance().Find("Pular") == nullptr);
    CHECK(&scene.Find(id)->components[0].Type() == &ComponentRegistry::MissingType());
    const std::string json = SceneSerializer::ToJson(scene, assets);
    CHECK(Contains(json, "\"velocidade\": 12.5") && Contains(json, "\"voltas\": 3"));

    return TestResult();
}
