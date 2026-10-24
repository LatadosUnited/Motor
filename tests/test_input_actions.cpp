// Controle e mapa de ações (Fase 21) sem janela: nomes de teclas e botões (ida e volta), mapa padrão, JSON (com
// entradas desconhecidas), valor das ações com teclas e um controle simulado (soma, limite, sentido, apertou/
// soltou), o mapa no .mproject e a API dos scripts (com e sem o foco do jogo).
#include "TestUtil.h"

#include <motor/core/Input.h>
#include <motor/core/Paths.h>
#include <motor/project/InputActions.h>
#include <motor/project/Project.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>

#include <cmath>
#include <fstream>
#include <string>

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
    void OnUpdate(float) override {
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
        for (const char* name : {"Horizontal", "Vertical", "Olhar X", "Olhar Y", "Pular", "Correr", "Interagir", "Pausa"}) {
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
        runtime.Update(0.016f);
        const Leitor* l = scene.FindByName("L")->Get<Leitor>();
        CHECK(Near(l->horizontal, 0.5f) && l->pulou && l->botaoA && Near(l->analogico, -0.5f));
        runtime.SetInputEnabled(false);  // digitando no editor
        runtime.Update(0.016f);
        CHECK(l->horizontal == 0.0f && !l->pulou && !l->botaoA && l->analogico == 0.0f);
        runtime.Stop();
        Input::SetKeyState(Key::Right, false);
        Input::SetGamepadSimulated(false);
        registry.Unregister(registry.Find("Leitor"));
    }

    return TestResult();
}
