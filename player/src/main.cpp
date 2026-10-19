// Motor Player: roda um jogo sem o editor (Fase 10). É o executável que a exportação copia com o nome do jogo.
//
//   <Jogo>.exe                    abre a pasta Dados/ ao lado do executável (jogo exportado)
//   Player.exe "<pasta ou .mproject>"   roda um projeto direto (para testar sem exportar)
//   --quadros N                   fecha sozinho depois de N quadros (testes automáticos)
//
// Abre o projeto, carrega a DLL de scripts, abre a cena inicial e roda o jogo como o Play do editor: o mesmo
// SceneRuntime (scripts, física, HUD e os outros sistemas registrados), a câmera principal da cena e a UI
// por cima. O log vai para "<Jogo>.log" ao lado do executável (ou na pasta temporária).

#include <motor/Motor.h>
#include <motor/scene/ScriptModule.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;
using namespace motor;

namespace {

// Pasta do projeto dentro do jogo exportado e nome da DLL de scripts (editor/src/export/GameExport.h).
constexpr const char* kDataFolder = "Dados";
constexpr const char* kScriptsDll = "GameScripts.dll";

struct Options {
    fs::path project;  // pasta ou .mproject; vazio = <pasta do executável>/Dados
    int frames = 0;    // > 0: fecha depois desse número de quadros
};

std::vector<std::string> Arguments(int argc, char** argv) {
    std::vector<std::string> args;
#ifdef _WIN32
    // argv do MinGW vem na página de código local: acentos se perdem. A linha de comando wide não.
    (void)argc;
    (void)argv;
    int count = 0;
    if (wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 1; i < count; ++i) args.push_back(ToUtf8(fs::path(wide[i])));
        LocalFree(wide);
    }
#else
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
#endif
    return args;
}

void ShowFatal(const std::string& message) {
    log::Error("{}", message);
#ifdef _WIN32
    const std::wstring text = PathFromUtf8(message).wstring();
    MessageBoxW(nullptr, text.c_str(), L"Erro ao abrir o jogo", MB_OK | MB_ICONERROR);
#endif
}

// Log num arquivo: o Player não tem console.
class LogFile {
public:
    explicit LogFile(const std::string& game) {
        const std::string name = game + ".log";
        for (const fs::path& dir : {ExecutableDir(), fs::temp_directory_path()}) {
            m_out.open(dir / PathFromUtf8(name), std::ios::binary | std::ios::trunc);
            if (m_out) break;
        }
        if (!m_out) return;
        m_sink = log::AddSink([this](log::Level level, std::string_view message) {
            static constexpr const char* kTags[] = {"trace", "info ", "warn ", "error"};
            std::lock_guard lock(m_mutex);
            m_out << '[' << kTags[static_cast<int>(level)] << "] " << message << '\n';
            m_out.flush();
        });
    }
    ~LogFile() {
        if (m_sink) log::RemoveSink(m_sink);
    }

private:
    std::ofstream m_out;
    std::mutex m_mutex;
    int m_sink = 0;
};

// DLL de scripts do projeto: a exportada (Dados/GameScripts.dll) ou a que o editor compila no projeto.
class ScriptLibrary {
public:
    ~ScriptLibrary() { Unload(); }

    // true se não há scripts ou se carregou. Os tipos ficam registrados até o Unload.
    bool Load(const fs::path& projectRoot, std::string& error) {
        std::error_code ec;
        fs::path dll = projectRoot / kScriptsDll;
        if (!fs::is_regular_file(dll, ec)) dll = projectRoot / ".motor" / "scripts" / "build" / kScriptsDll;
        if (!fs::is_regular_file(dll, ec)) {
            if (fs::is_directory(projectRoot / "Scripts", ec) && !fs::is_empty(projectRoot / "Scripts", ec)) {
                log::Warn("Scripts: o projeto tem a pasta Scripts mas a DLL não foi compilada; o jogo roda sem scripts");
            }
            return true;
        }
#ifdef _WIN32
        HMODULE module = LoadLibraryW(dll.c_str());
        if (!module) {
            error = std::format("Não foi possível carregar os scripts ({}): erro {}", ToUtf8(dll), GetLastError());
            return false;
        }
        const auto version = reinterpret_cast<ScriptApiVersionFn>(
            reinterpret_cast<void*>(GetProcAddress(module, kScriptApiVersionSymbol)));
        const auto registerFn = reinterpret_cast<RegisterScriptsFn>(
            reinterpret_cast<void*>(GetProcAddress(module, kRegisterScriptsSymbol)));
        if (!version || !registerFn || version() != kScriptApiVersion) {
            error = std::format("A DLL de scripts não é desta versão do Motor (API {}, esperado {}). Exporte o jogo de novo.",
                                version ? version() : -1, kScriptApiVersion);
            FreeLibrary(module);
            return false;
        }
        ComponentRegistry& registry = ComponentRegistry::Instance();
        std::vector<const ComponentType*> before;
        for (const auto& t : registry.Types()) before.push_back(t.get());
        registerFn(registry);
        for (const auto& t : registry.Types()) {
            if (std::find(before.begin(), before.end(), t.get()) == before.end()) m_types.push_back(t.get());
        }
        m_module = module;
        log::Info("Scripts carregados: {} tipo(s)", m_types.size());
        return true;
#else
        error = "carregar scripts só é suportado no Windows por enquanto";
        return false;
#endif
    }

    // Só depois que nenhuma cena tiver mais componentes dos scripts.
    void Unload() {
#ifdef _WIN32
        if (!m_module) return;
        for (const ComponentType* t : m_types) ComponentRegistry::Instance().Unregister(t);
        m_types.clear();
        FreeLibrary(static_cast<HMODULE>(m_module));
        m_module = nullptr;
#endif
    }

private:
    void* m_module = nullptr;
    std::vector<const ComponentType*> m_types;
};

class Player final : public Application {
public:
    Player(Project project, const Options& options, const ApplicationDesc& desc)
        : Application(desc), m_project(std::move(project)), m_options(options) {}

    bool Failed() const { return !m_fatal.empty(); }
    const std::string& FatalError() const { return m_fatal; }

private:
    void OnInit() override {
        AddBuiltinAssets(m_assets);
        m_assets.SetRoot(m_project.RootDir());
        PhysicsSettings::Global().layers = m_project.Layers();  // camadas de colisão do projeto
        m_ui = std::make_unique<UiRenderer>();

        std::string error;
        if (!m_scripts.Load(m_project.RootDir(), error)) return Fail(error);

        const fs::path startup = m_project.StartupScene();
        if (startup.empty()) return Fail("O projeto não tem cena inicial.");
        if (!SceneSerializer::Load(startup, m_scene, m_assets, &error)) {
            return Fail(std::format("Não foi possível abrir a cena inicial '{}': {}", ToUtf8(startup), error));
        }
        log::Info("Jogo: {} | cena inicial: {}", m_project.Name(), m_project.ToRelative(startup));
        StartRuntime();
    }

    void OnFixedUpdate(float dt) override {
        if (!m_runtime) return;
        m_runtime->FixedUpdate(dt);
        ApplySceneRequest();
    }

    void OnUpdate(float dt) override {
        if (Failed()) return Quit();  // o erro do OnInit (o Run só começa a valer o Quit depois dele)
        if (!m_runtime) return;
        // Mouse para os botões da UI do jogo, na tela inteira.
        if (UiSystem* ui = m_runtime->GetSystem<UiSystem>()) {
            const Window& window = GetWindow();
            ui->SetPointer({{static_cast<float>(window.Width()), static_cast<float>(window.Height())},
                            Input::MousePosition(), !window.IsMinimized(), Input::IsMouseDown(MouseButton::Left)});
        }
        m_runtime->Update(dt);
        ApplySceneRequest();
        if (m_runtime) GetWindow().SetCursorMode(m_runtime->GetCursorMode());  // pedido pelos scripts

        if (m_options.frames > 0 && ++m_frame >= m_options.frames) {
            log::Info("Jogo: {} quadros rodados (--quadros), fechando", m_frame);
            Quit();
        }
    }

    void OnRender(Renderer& renderer) override {
        if (!m_runtime) return;
        const Window& window = GetWindow();
        // Pela câmera principal da cena; sem ela, de um ponto fixo olhando a origem.
        Camera camera;
        if (!m_scene.ApplyCamera(m_scene.FindMainCamera(), camera)) {
            if (!m_warnedCamera) log::Warn("A cena não tem câmera principal: usando uma câmera fixa");
            m_warnedCamera = true;
            camera.position = {0.0f, 3.0f, 8.0f};
            camera.LookAt({0.0f, 0.0f, 0.0f});
        }
        camera.aspect = window.AspectRatio();
        renderer.BeginScene(camera, m_scene.CollectLights(), m_scene.environment);
        m_scene.SubmitTo(renderer);
        renderer.EndScene();
        m_ui->Render(m_scene, m_assets, window.Width(), window.Height());
    }

    void OnShutdown() override {
        if (m_runtime) m_runtime->Stop();
        m_runtime.reset();
        m_scene = Scene();  // os componentes dos scripts somem antes de descarregar a DLL
        m_ui.reset();
        m_scripts.Unload();
    }

    void StartRuntime() {
        m_runtime = std::make_unique<SceneRuntime>(m_scene);
        m_runtime->SetAssets(&m_assets);
        m_runtime->Start();
    }

    // Script::LoadScene: carrega antes de parar a cena atual (se falhar, o jogo continua nela).
    void ApplySceneRequest() {
        if (!m_runtime || !m_runtime->HasSceneRequest()) return;
        const std::string request = m_runtime->TakeSceneRequest();
        const fs::path file = FindSceneFile(m_assets, request);
        Scene next;
        std::string error = "cena não encontrada no jogo";
        if (file.empty() || !SceneSerializer::Load(file, next, m_assets, &error)) {
            log::Error("LoadScene(\"{}\"): {}", request, error);
            return;
        }
        const CursorMode cursor = m_runtime->GetCursorMode();  // continua na cena nova
        m_runtime->Stop();
        m_runtime.reset();
        m_scene = std::move(next);
        log::Info("Cena trocada pelo jogo: {}", request);
        StartRuntime();
        if (m_runtime) m_runtime->SetCursorMode(cursor);
    }

    void Fail(std::string message) { m_fatal = std::move(message); }

    Project m_project;
    Options m_options;
    AssetLibrary m_assets;
    ScriptLibrary m_scripts;  // declarado antes da cena: é destruído depois dela
    Scene m_scene;
    std::unique_ptr<SceneRuntime> m_runtime;
    std::unique_ptr<UiRenderer> m_ui;
    std::string m_fatal;
    int m_frame = 0;
    bool m_warnedCamera = false;
};

} // namespace

int main(int argc, char** argv) {
    Options options;
    const std::vector<std::string> args = Arguments(argc, argv);
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--quadros" && i + 1 < args.size()) {
            options.frames = std::atoi(args[++i].c_str());
        } else if (!args[i].starts_with("--")) {
            options.project = PathFromUtf8(args[i]);
        }
    }
    if (options.project.empty()) options.project = ExecutableDir() / kDataFolder;

    std::string error;
    auto project = Project::Open(options.project, &error);
    if (!project) {
        ShowFatal(std::format("Não foi possível abrir o jogo em '{}':\n{}", ToUtf8(options.project), error));
        return 1;
    }
    LogFile logFile(project->Name());

    try {
        ApplicationDesc desc;
        desc.window.title = project->Name();
        Player player(std::move(*project), options, desc);
        player.Run();
        if (player.Failed()) {
            ShowFatal(player.FatalError());
            return 1;
        }
    } catch (const std::exception& e) {
        ShowFatal(std::format("Erro: {}", e.what()));
        return 1;
    }
    return 0;
}
