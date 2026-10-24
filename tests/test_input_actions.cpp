// Controle e mapa de ações (Fase 21, v2 na alpha 4) sem janela: nomes de teclas e botões (ida e volta), mapa
// padrão, JSON (com entradas desconhecidas e a versão 1), valor das ações com teclas e um controle simulado (soma,
// limite, sentido, apertou/soltou), tipos (botão, eixo, vetor 2D), o mouse como eixo, teclas de junto, o mapa no
// .mproject e a API dos scripts (com e sem o foco do jogo).
#include "TestUtil.h"

#include <motor/core/Input.h>
#include <motor/core/Paths.h>
#include <motor/project/InputActions.h>
#include <motor/project/Project.h>
#include <motor/scene/GameHost.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/ui/Ui.h>

#include <cmath>
#include <fstream>
#include <string>
#include <utility>

using namespace motor;
namespace fs = std::filesystem;

namespace {

bool Near(float a, float b) { return std::abs(a - b) < 1e-4f; }

// Um quadro: o estado atual vira o "anterior" (como no começo de cada frame da Application).
void NextFrame() { Input::BeginFrame(); }

struct Leitor : Script {
    float horizontal = 0.0f;
    bool pulou = false;
    bool botaoA = false;
    float analogico = 0.0f;
    glm::vec2 mover{0.0f};
    std::string texto;
    std::string trocarPara;
    bool menuAtivo = false, confirmar = false, pular = false, mouse = false, atirar = false, sobreUi = false;
    void OnUpdate(float) override {
        mover = GetVector("Mover");
        texto = TextInput();
        if (!trocarPara.empty()) SetInputContext(std::exchange(trocarPara, {}));
        menuAtivo = IsInputContextActive("menu");
        confirmar = IsActionDown("Confirmar");
        pular = IsActionDown("Pular");
        mouse = IsMouseDown(MouseButton::Left);
        atirar = IsActionDown("Atirar");
        sobreUi = IsPointerOverUi();
        horizontal = GetAxis("Horizontal");
        pulou = WasActionPressed("Pular");
        botaoA = IsGamepadButtonDown(GamepadButton::A);
        analogico = GamepadAxisValue(GamepadAxis::LeftX);
    }
};

} // namespace

int main(int argc, char** argv) {
    const fs::path dir = TestDir(argc, argv, "test_input_actions_tmp");

    SECTION("Nomes: ida e volta");
    {
        int named = 0;
        for (int k = 0; k < 400; ++k) {
            const char* name = KeyName(static_cast<Key>(k));
            if (!*name) continue;
            ++named;
            const auto back = ParseKeyName(name);
            CHECK(back && static_cast<int>(*back) == k);
        }
        CHECK(named == 119);  // letras, dígitos, F1 a F24, Numpad0 a 9 e as 49 com nome
        CHECK(std::string(KeyName(Key::F11)) == "F11" && std::string(KeyName(Key::F2)) == "F2");
        CHECK(ParseKeyName("space") == Key::Space && ParseKeyName("leftshift") == Key::LeftShift);
        CHECK(!ParseKeyName("Tecla de mentira"));
        for (int b = 0; b < kGamepadButtonCount; ++b) {
            CHECK(ParseGamepadButton(GamepadButtonName(static_cast<GamepadButton>(b))) == static_cast<GamepadButton>(b));
        }
        for (int a = 0; a < kGamepadAxisCount; ++a) {
            CHECK(ParseGamepadAxis(GamepadAxisName(static_cast<GamepadAxis>(a))) == static_cast<GamepadAxis>(a));
        }
        CHECK(ParseMouseButton("MouseLeft") == MouseButton::Left);
    }

    SECTION("Mapa padrão e JSON");
    const InputActions defaults = InputActions::Defaults();
    {
        for (const char* name : {"Mover", "Olhar", "Horizontal", "Vertical", "Olhar X", "Olhar Y", "Pular", "Correr",
                                 "Interagir", "Pausa", "Navegar", "Confirmar", "Voltar"}) {
            CHECK(defaults.Find(name) != nullptr);
        }
        CHECK(defaults.Find("pular") == defaults.Find("Pular"));  // sem diferença de maiúsculas
        InputActions back;
        CHECK(back.FromJson(defaults.ToJson()));
        CHECK(back == defaults);

        std::string problems;
        InputActions parsed;
        CHECK(parsed.FromJson(R"({"actions": [
            {"name": "Atirar", "bindings": [{"key": "F"}, {"mouse": "MouseLeft"}, {"gamepad": "RightBumper"},
                                            {"axis": "RightTrigger"}, {"key": "Tecla Nova"}, {"gamepad": "Z"}]},
            {"name": "atirar", "bindings": []},
            {"bindings": []}]})", &problems));
        CHECK(parsed.actions.size() == 1);  // a repetida e a sem nome ficam de fora
        CHECK(parsed.actions[0].bindings.size() == 4);
        CHECK(problems.find("Tecla Nova") != std::string::npos && problems.find("'Z'") != std::string::npos);
        CHECK(!parsed.FromJson("[1, 2]", &problems));
        CHECK(parsed.actions.size() == 1);  // inválido: não mexe
        CHECK(InputBinding::Of(GamepadAxis::LeftY, -1).Label() == "Analógico LeftY (-)");
    }

    SECTION("Valores com teclado");
    {
        NextFrame();
        Input::SetKeyState(Key::D, true);
        CHECK(Near(defaults.Value("Horizontal"), 1.0f));
        Input::SetKeyState(Key::A, true);
        CHECK(Near(defaults.Value("Horizontal"), 0.0f));  // os dois: se anulam
        Input::SetKeyState(Key::D, false);
        Input::SetKeyState(Key::Left, true);
        CHECK(Near(defaults.Value("Horizontal"), -1.0f));  // A e seta: soma -2, limitada a -1
        Input::SetKeyState(Key::A, false);
        Input::SetKeyState(Key::Left, false);

        NextFrame();
        Input::SetKeyState(Key::Space, true);
        CHECK(defaults.IsDown("Pular") && defaults.WasPressed("Pular"));
        NextFrame();
        CHECK(defaults.IsDown("Pular") && !defaults.WasPressed("Pular"));
        Input::SetKeyState(Key::Space, false);
        CHECK(defaults.WasReleased("Pular"));
        NextFrame();
        CHECK(!defaults.WasReleased("Pular"));
        CHECK(defaults.Value("Ação que não existe") == 0.0f && !defaults.IsDown("Ação que não existe"));
    }

    SECTION("Controle simulado");
    {
        Input::SetGamepadSimulated(true);
        CHECK(Input::IsGamepadConnected() && !Input::GamepadName().empty());
        Input::SetGamepadAxisState(GamepadAxis::LeftX, 0.4f);
        CHECK(Near(defaults.Value("Horizontal"), 0.4f));
        CHECK(!defaults.IsDown("Horizontal"));
        Input::SetGamepadAxisState(GamepadAxis::LeftY, -0.8f);
        CHECK(Near(defaults.Value("Vertical"), -0.8f));
        Input::SetGamepadAxisState(GamepadAxis::LeftTrigger, -1.0f);  // gatilho não vai abaixo de 0
        CHECK(Input::GamepadAxisValue(GamepadAxis::LeftTrigger) == 0.0f);
        NextFrame();
        Input::SetGamepadButtonState(GamepadButton::A, true);
        CHECK(defaults.WasPressed("Pular") && Input::WasGamepadButtonPressed(GamepadButton::A));
        Input::SetGamepadButtonState(GamepadButton::DpadLeft, true);
        CHECK(Near(defaults.Value("Horizontal"), -0.6f));  // analógico 0.4 + direcional -1
        Input::PollGamepads();  // simulado: o controle de verdade não sobrescreve
        CHECK(Input::IsGamepadButtonDown(GamepadButton::A));
        Input::SetGamepadSimulated(false);
        CHECK(!Input::IsGamepadButtonDown(GamepadButton::A) && Input::GamepadAxisValue(GamepadAxis::LeftX) == 0.0f);
    }

    SECTION("No .mproject");
    {
        std::string error;
        auto project = Project::Create(dir, "Jogo Ação", &error);
        CHECK(project.has_value());
        if (!project) return TestResult();
        CHECK(project->Actions() == defaults);
        InputActions mine;
        mine.actions = {{"Atirar", {InputBinding::Of(Key::F), InputBinding::Of(GamepadAxis::RightTrigger)}}};
        project->SetActions(mine);
        CHECK(project->Save(&error));
        auto reopened = Project::Open(project->RootDir(), &error);
        CHECK(reopened && reopened->Actions() == mine);

        const fs::path old = dir / "Antigo";
        fs::create_directories(old);
        std::ofstream(old / "Antigo.mproject") << R"({"format": "motor-project", "version": 3, "name": "Antigo"})";
        auto antigo = Project::Open(old, &error);
        CHECK(antigo && antigo->Actions() == defaults);  // sem mapa: o padrão
    }

    SECTION("Scripts: ações e controle, só com o foco do jogo");
    {
        auto& registry = ComponentRegistry::Instance();
        registry.Register<Leitor>("Leitor", "Leitor");
        Scene scene;
        scene.CreateEntity("L").Add<Leitor>();
        SceneRuntime runtime(scene);  // sem host: o mapa padrão
        runtime.Start();
        Input::SetGamepadSimulated(true);
        NextFrame();
        Input::SetKeyState(Key::Right, true);
        Input::SetGamepadButtonState(GamepadButton::A, true);
        Input::SetGamepadAxisState(GamepadAxis::LeftX, -0.5f);
        Input::AddTextInput("olá");
        runtime.Update(0.016f);
        const Leitor* l = scene.FindByName("L")->Get<Leitor>();
        CHECK(Near(l->horizontal, 0.5f) && l->pulou && l->botaoA && Near(l->analogico, -0.5f));
        CHECK(Near(l->mover.x, 0.5f) && Near(l->mover.y, 0.0f) && l->texto == "olá");
        runtime.SetInputEnabled(false);  // digitando no editor
        runtime.Update(0.016f);
        CHECK(l->horizontal == 0.0f && !l->pulou && !l->botaoA && l->analogico == 0.0f);
        CHECK(l->mover == glm::vec2(0.0f) && l->texto.empty());
        runtime.Stop();
        Input::SetKeyState(Key::Right, false);
        Input::SetGamepadSimulated(false);
        registry.Unregister(registry.Find("Leitor"));
    }

    SECTION("Vetor 2D: a diagonal não passa de 1");
    {
        NextFrame();  // a seta solta no fim da seção anterior ainda conta naquele quadro
        NextFrame();
        Input::SetKeyState(Key::D, true);
        CHECK(Near(defaults.Vector("Mover").x, 1.0f) && Near(defaults.Vector("Mover").y, 0.0f));
        CHECK(defaults.WasPressed("Mover"));
        Input::SetKeyState(Key::W, true);
        const glm::vec2 diagonal = defaults.Vector("Mover");
        CHECK(Near(diagonal.x, 0.70710677f) && Near(diagonal.y, 0.70710677f));
        CHECK(Near(defaults.Value("Mover"), 1.0f));  // o comprimento
        CHECK(Near(defaults.Vector("Horizontal").x, 1.0f) && defaults.Vector("Horizontal").y == 0.0f);
        Input::SetKeyState(Key::D, false);
        Input::SetKeyState(Key::W, false);
        NextFrame();
        Input::SetGamepadSimulated(true);
        Input::SetGamepadAxisState(GamepadAxis::LeftX, 0.3f);
        Input::SetGamepadAxisState(GamepadAxis::LeftY, -0.4f);
        CHECK(Near(defaults.Vector("Mover").x, 0.3f) && Near(defaults.Vector("Mover").y, -0.4f));  // dentro: como veio
        Input::SetGamepadSimulated(false);
        NextFrame();
    }

    SECTION("Botão fica entre 0 e 1");
    {
        InputActions mine;
        mine.actions = {{"Atirar", {InputBinding::Of(Key::F), InputBinding::Of(Key::G), InputBinding::Of(Key::H, -1)},
                         InputActionType::Button}};
        Input::SetKeyState(Key::F, true);
        Input::SetKeyState(Key::G, true);
        CHECK(Near(mine.Value("Atirar"), 1.0f));
        Input::SetKeyState(Key::F, false);
        Input::SetKeyState(Key::G, false);
        NextFrame();
        Input::SetKeyState(Key::H, true);
        CHECK(mine.Value("Atirar") == 0.0f);
        Input::SetKeyState(Key::H, false);
        NextFrame();
    }

    SECTION("Mouse como eixo, na medida do analógico");
    {
        NextFrame();
        Input::SetMousePosition({100.0f, 100.0f});
        Input::BeginFrame(0.01f);
        Input::SetMousePosition({110.0f, 95.0f});  // 10 px à direita e 5 para cima em 0,01 s
        CHECK(Near(Input::MouseVelocity().x, 1000.0f) && Near(Input::MouseVelocity().y, -500.0f));
        const glm::vec2 olhar = defaults.Vector("Olhar");
        CHECK(Near(olhar.x, 1.0f) && Near(olhar.y, 0.5f));  // Y para cima positivo
        CHECK(Near(defaults.Value("Olhar X"), 1.0f) && Near(defaults.Value("Olhar Y"), 0.5f));
        // Valor * dt é o giro: 10 px dão o mesmo giro num quadro de 0,01 s ou de 0,02 s.
        const float giro = olhar.x * 0.01f;
        Input::BeginFrame(0.02f);
        Input::SetMousePosition({120.0f, 95.0f});
        CHECK(Near(defaults.Vector("Olhar").x * 0.02f, giro));
        Input::BeginFrame(0.01f);
        Input::SetMousePosition({150.0f, 95.0f});  // rápido: 3000 px/s
        Input::SetGamepadSimulated(true);
        Input::SetGamepadAxisState(GamepadAxis::RightX, 1.0f);
        CHECK(Near(defaults.Value("Olhar X"), 4.0f));  // o mouse soma depois do limite do analógico
        Input::SetGamepadSimulated(false);
        Input::BeginFrame(0.01f);
        CHECK(Near(Input::MouseVelocityLastFrame().x, 3000.0f) && defaults.Vector("Olhar") == glm::vec2(0.0f));
        NextFrame();
    }

    SECTION("Tecla de junto (Ctrl+S)");
    {
        InputActions mine;
        mine.actions = {{"Salvar", {InputBinding::Of(Key::S).With(Key::LeftControl)}, InputActionType::Button}};
        CHECK(mine.actions[0].bindings[0].Label() == "LeftControl + Tecla S");
        Input::SetKeyState(Key::S, true);
        CHECK(!mine.IsDown("Salvar"));
        Input::SetKeyState(Key::RightControl, true);  // o lado direito também vale
        CHECK(mine.IsDown("Salvar") && mine.WasPressed("Salvar"));
        Input::SetKeyState(Key::RightControl, false);
        NextFrame();
        Input::SetKeyState(Key::S, false);
        NextFrame();
        Input::SetKeyState(Key::LeftControl, true);
        Input::SetKeyState(Key::S, true);
        CHECK(mine.IsDown("Salvar"));
        Input::SetKeyState(Key::LeftControl, false);
        Input::SetKeyState(Key::S, false);
        NextFrame();
    }

    SECTION("JSON v2 e v1");
    {
        InputActions back;
        CHECK(back.FromJson(defaults.ToJson()) && back == defaults);  // tipos, part e mouse vão e voltam
        InputActions combo;
        combo.actions = {{"Salvar", {InputBinding::Of(Key::S, 0.5f).With(Key::LeftControl)}, InputActionType::Button}};
        CHECK(back.FromJson(combo.ToJson()) && back == combo);
        CHECK(combo.ToJson().find(R"("version": 2)") != std::string::npos);

        std::string problems;
        CHECK(back.FromJson(R"({"actions": [{"name": "Velha", "bindings": [{"key": "A", "scale": -1}]}]})", &problems));
        CHECK(problems.empty() && back.actions[0].type == InputActionType::Axis);  // v1: tudo eixo
        CHECK(back.FromJson(R"({"version": 2, "actions": [
            {"name": "X", "type": "gatilho", "bindings": [{"key": "S", "with": "Tecla Nova"}, {"key": "D", "part": 3},
                                                         {"mouse": "MouseScroll", "part": "Y"}]}]})", &problems));
        CHECK(back.actions[0].type == InputActionType::Axis && back.actions[0].bindings.size() == 2);
        CHECK(problems.find("gatilho") != std::string::npos && problems.find("Tecla Nova") != std::string::npos);
        CHECK(back.actions[0].bindings[1].source == InputBinding::Source::MouseAxis);
        CHECK(back.actions[0].bindings[1].part == InputBinding::Part::Y);
        CHECK(ParseMouseAxis("mousex") == MouseAxis::X && std::string(MouseAxisName(MouseAxis::Scroll)) == "MouseScroll");
    }

    SECTION("Contextos: Jogo e Menu");
    {
        CHECK(defaults.startContext == "Jogo" && defaults.Find("Pular")->context == "Jogo");
        CHECK(defaults.Find("Confirmar")->context == "Menu");
        const std::vector<std::string> jogo{"Jogo"}, menu{"menu"};  // sem diferença de maiúsculas
        InputFilter noJogo, noMenu;
        noJogo.contexts = &jogo;
        noMenu.contexts = &menu;
        NextFrame();
        NextFrame();
        Input::SetKeyState(Key::Space, true);
        CHECK(defaults.IsDown("Pular", noJogo) && !defaults.IsDown("Confirmar", noJogo));
        CHECK(!defaults.IsDown("Pular", noMenu) && defaults.IsDown("Confirmar", noMenu));
        CHECK(defaults.IsDown("Pular") && defaults.IsDown("Confirmar"));  // sem filtro (editor): todas
        NextFrame();
        // Espaço segurado desde o Jogo: abrir o menu agora não é um "Confirmar" novo.
        CHECK(defaults.IsDown("Confirmar", noMenu) && !defaults.WasPressed("Confirmar", noMenu));
        Input::SetKeyState(Key::Space, false);
        NextFrame();

        InputActions sem;  // ação sem contexto vale em qualquer um
        sem.actions = {{"Sempre", {InputBinding::Of(Key::K)}, InputActionType::Button}};
        Input::SetKeyState(Key::K, true);
        CHECK(sem.IsDown("Sempre", noMenu) && sem.IsDown("Sempre", noJogo));
        Input::SetKeyState(Key::K, false);
        NextFrame();

        InputActions back;
        InputActions custom = defaults;
        custom.startContext = "Menu";
        CHECK(back.FromJson(custom.ToJson()) && back == custom && back.startContext == "Menu");
        CHECK(back.FromJson(R"({"actions": []})") && back.startContext == "Jogo");  // sem o campo: o padrão
    }

    SECTION("Scripts: contextos e o clique da interface");
    {
        auto& registry = ComponentRegistry::Instance();
        registry.Register<Leitor>("Leitor", "Leitor");
        Scene scene;
        scene.CreateEntity("L").Add<Leitor>();
        Entity& botao = scene.CreateEntity("Jogar");
        UiRect& rect = botao.Add<UiRect>();
        rect.anchor = UiAnchor::Center;
        rect.size = {320, 90};
        botao.Add<UiButton>();
        const glm::vec2 tela{1920, 1080}, sobre{960, 540}, fora{100, 100};
        InputActions comAtirar = InputActions::Defaults();
        comAtirar.actions.push_back({"Atirar", {InputBinding::Of(MouseButton::Left)}, InputActionType::Button, "Jogo"});
        struct Host : GameHost {
            InputActions actions;
            SaveStore saves;
            GameOptions Options() const override { return {}; }
            void ApplyOptions(const GameOptions&) override {}
            std::vector<Resolution> Resolutions() const override { return {}; }
            SaveStore& Saves() override { return saves; }
            const InputActions& Actions() const override { return actions; }
            std::string GameVersion() const override { return {}; }
            void QuitGame() override {}
        } host;
        host.actions = comAtirar;
        SceneRuntime runtime(scene);
        runtime.SetHost(&host);
        runtime.Start();
        Leitor* l = scene.FindByName("L")->Get<Leitor>();
        UiSystem* ui = runtime.GetSystem<UiSystem>();
        CHECK(ui != nullptr);
        const auto frame = [&](glm::vec2 mouse, bool down) {
            NextFrame();
            Input::SetMouseButtonState(MouseButton::Left, down);
            if (ui) ui->SetPointer({tela, mouse, true, down});
            runtime.Update(0.016f);
        };

        Input::SetKeyState(Key::Space, true);
        frame(fora, false);
        CHECK(!l->menuAtivo && l->pular && !l->confirmar);  // começa no Jogo
        l->trocarPara = "Menu";  // o script troca e já lê no contexto novo
        frame(fora, false);
        CHECK(l->menuAtivo && !l->pular && l->confirmar);
        CHECK(runtime.InputContexts().size() == 1 && runtime.InputContexts()[0] == "Menu");
        Input::SetKeyState(Key::Space, false);
        l->trocarPara = "Jogo";
        frame(fora, false);

        // Clique que começa no botão é da interface: o jogo não atira, nem arrastando para fora.
        frame(sobre, false);
        CHECK(l->sobreUi && !l->mouse);
        frame(sobre, true);
        CHECK(!l->mouse && !l->atirar);
        frame(fora, true);
        CHECK(!l->mouse && !l->atirar && !l->sobreUi);
        frame(fora, false);
        // Clique fora da interface é do jogo, mesmo passando por cima dela depois.
        frame(fora, true);
        CHECK(l->mouse && l->atirar);
        frame(sobre, true);
        CHECK(l->mouse && l->atirar && l->sobreUi);
        frame(sobre, false);
        // Cursor preso (primeira pessoa): a mira no centro não é um clique na interface.
        runtime.SetCursorMode(CursorMode::Locked);
        frame(sobre, true);
        CHECK(l->mouse && l->atirar && !l->sobreUi);
        frame(sobre, false);

        runtime.Stop();
        registry.Unregister(registry.Find("Leitor"));
        NextFrame();
    }

    SECTION("Teclas do jogador por cima do mapa");
    {
        InputActions player;
        player.actions = {{"Pular", {InputBinding::Of(Key::J), InputBinding::Of(GamepadButton::B)}},
                          {"Ação que saiu do mapa", {InputBinding::Of(Key::K)}}};
        const InputActions mixed = defaults.WithPlayerBindings(player);
        CHECK(mixed.Find("Pular")->bindings == player.actions[0].bindings);
        CHECK(mixed.Find("Pular")->type == InputActionType::Button && mixed.Find("Pular")->context == "Jogo");
        CHECK(mixed.Find("Correr")->bindings == defaults.Find("Correr")->bindings);  // as outras não mudam
        CHECK(!mixed.Find("Ação que saiu do mapa") && mixed.actions.size() == defaults.actions.size());

        // Conflitos: só no mesmo contexto (ou sem contexto).
        const auto space = defaults.Conflicts("Interagir", InputBinding::Of(Key::Space));
        CHECK(space.size() == 1 && space[0] == "Pular");  // Confirmar também é Espaço, mas no Menu
        CHECK(defaults.Conflicts("Confirmar", InputBinding::Of(Key::Space)).empty());
        CHECK(defaults.Conflicts("Interagir", InputBinding::Of(Key::Space).With(Key::LeftControl)).empty());
        CHECK(defaults.Conflicts("Pular", InputBinding::Of(Key::Space)).empty());  // ela mesma não conta

        // Arquivo controles.json.
        const fs::path file = dir / "Jogador" / "controles.json";
        CHECK(SavePlayerBindings(file, player));
        InputActions back;
        CHECK(LoadPlayerBindings(file, back) && back.actions == player.actions);
        CHECK(!LoadPlayerBindings(dir / "nada.json", back) && back.actions.empty());
    }

    SECTION("Aperte a nova tecla");
    {
        NextFrame();
        NextFrame();
        CHECK(!InputActions::FirstPressed());
        Input::SetKeyState(Key::G, true);
        CHECK(InputActions::FirstPressed() == InputBinding::Of(Key::G));
        NextFrame();
        CHECK(!InputActions::FirstPressed());  // segurada não se repete
        Input::SetKeyState(Key::G, false);
        Input::SetMouseButtonState(MouseButton::Right, true);
        CHECK(InputActions::FirstPressed() == InputBinding::Of(MouseButton::Right));
        CHECK(!InputActions::FirstPressed(false));  // sem o mouse (editor)
        Input::SetMouseButtonState(MouseButton::Right, false);
        NextFrame();
        Input::SetGamepadSimulated(true);
        Input::SetGamepadAxisState(GamepadAxis::RightY, -0.9f);
        CHECK(InputActions::FirstPressed() == InputBinding::Of(GamepadAxis::RightY, -1.0f));
        NextFrame();
        CHECK(!InputActions::FirstPressed());  // continua empurrado: não é um aperto novo
        Input::SetGamepadSimulated(false);
        NextFrame();
    }

    SECTION("Scripts: trocar e restaurar as teclas");
    {
        struct Trocador : Script {
            bool trocou = false;
            std::vector<InputBinding> pular;
            std::vector<std::string> conflitos;
            void OnUpdate(float) override {
                if (auto nova = GetPressedBinding()) {
                    conflitos = GetBindingConflicts("Mover", *nova);
                    trocou = RebindAction("Mover", 2, *nova);  // W (no Y de Mover)
                }
                pular = GetActionBindings("Pular");
            }
            // De fora do OnUpdate (o teste chama direto).
            bool Trocar(std::string_view action, int index, const InputBinding& b) { return RebindAction(action, index, b); }
            void Restaurar(std::string_view action = {}) { ResetActionBindings(action); }
        };
        struct Host : GameHost {
            InputActions project = InputActions::Defaults();
            InputActions player;
            InputActions actions = InputActions::Defaults();
            SaveStore saves;
            GameOptions Options() const override { return {}; }
            void ApplyOptions(const GameOptions&) override {}
            std::vector<Resolution> Resolutions() const override { return {}; }
            SaveStore& Saves() override { return saves; }
            const InputActions& Actions() const override { return actions; }
            InputActions PlayerBindings() const override { return player; }
            void SetPlayerBindings(const InputActions& p) override {
                player = p;
                actions = project.WithPlayerBindings(player);
            }
            std::string GameVersion() const override { return {}; }
            void QuitGame() override {}
        } host;
        auto& registry = ComponentRegistry::Instance();
        registry.Register<Trocador>("Trocador", "Trocador");
        Scene scene;
        scene.CreateEntity("T").Add<Trocador>();
        SceneRuntime runtime(scene);
        runtime.SetHost(&host);
        runtime.Start();
        Trocador* t = scene.FindByName("T")->Get<Trocador>();

        NextFrame();
        Input::SetKeyState(Key::I, true);  // o jogador escolhe I no lugar de W
        runtime.Update(0.016f);
        CHECK(t->trocou && t->conflitos.empty());
        const InputBinding& w = host.Actions().Find("Mover")->bindings[2];
        CHECK(w.source == InputBinding::Source::Key && w.code == static_cast<int>(Key::I));
        CHECK(w.part == InputBinding::Part::Y && w.scale == 1.0f);  // o lado e o sentido ficam
        CHECK(host.player.actions.size() == 1 && host.player.actions[0].name == "Mover");
        CHECK(Near(host.Actions().Vector("Mover").y, 1.0f));  // I anda para a frente
        Input::SetKeyState(Key::I, false);
        NextFrame();

        Input::SetKeyState(Key::Space, true);  // Espaço já é Pular, que está no mesmo contexto
        runtime.Update(0.016f);
        CHECK(t->conflitos.size() == 1 && t->conflitos[0] == "Pular");
        Input::SetKeyState(Key::Space, false);
        NextFrame();

        CHECK(!t->Trocar("Mover", 99, InputBinding::Of(Key::K)) && !t->Trocar("Nada", 0, InputBinding::Of(Key::K)));
        t->Restaurar("mover");
        CHECK(host.player.actions.empty() && host.Actions() == host.project);
        t->Trocar("Pular", 0, InputBinding::Of(Key::K));
        t->Trocar("Correr", 0, InputBinding::Of(Key::L));
        CHECK(host.player.actions.size() == 2);
        t->Restaurar();
        CHECK(host.player.actions.empty());
        runtime.Update(0.016f);
        CHECK(t->pular == defaults.Find("Pular")->bindings);

        runtime.Stop();
        registry.Unregister(registry.Find("Trocador"));
        NextFrame();
    }

    return TestResult();
}
