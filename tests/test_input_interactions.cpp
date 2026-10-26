// Ações no tempo sem janela (InputActionTracker): segurar por um tempo, toque curto, toque duplo (e o terceiro
// aperto começando outra dupla), buffer gasto uma vez, acompanhamento que começa sem histórico, jogo sem foco,
// contextos e a API dos scripts (buffer de pulo pelo SceneRuntime).
#include "TestUtil.h"

#include <motor/core/Input.h>
#include <motor/project/InputActionTracker.h>
#include <motor/project/InputActions.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>

#include <cmath>
#include <string>
#include <vector>

using namespace motor;

namespace {

constexpr float kDt = 0.1f;

bool Near(float a, float b) { return std::abs(a - b) < 1e-4f; }

// Um quadro: as teclas do quadro e o tracker atualizado (como o SceneRuntime faz antes dos scripts).
struct Quadros {
    InputActions actions;
    InputActionTracker tracker;
    InputFilter filter;
    bool enabled = true;
    void Frame(Key key, bool down) {
        Input::BeginFrame(kDt);
        Input::SetKeyState(key, down);
        tracker.Update(actions, filter, enabled, kDt);
    }
    void Frames(Key key, bool down, int count) {
        for (int i = 0; i < count; ++i) Frame(key, down);
    }
};

struct Pulador : Script {
    bool noChao = false;
    int pulos = 0;
    void OnUpdate(float) override {
        if (noChao && ConsumeBufferedAction("Pular", 0.15f)) ++pulos;
    }
};

} // namespace

int main() {
    Quadros q;
    q.actions.actions = {{"Pular", {InputBinding::Of(Key::Space)}, InputActionType::Button},
                         {"Menu", {InputBinding::Of(Key::M)}, InputActionType::Button, "Menu"}};

    SECTION("Começa sem histórico");
    {
        Input::SetKeyState(Key::Space, true);
        q.Frame(Key::Space, true);  // já estava apertada: não é um aperto
        CHECK(!q.tracker.ConsumeBuffered("Pular", 1.0f) && !q.tracker.WasDoubleTapped("Pular", 1.0f));
        q.Frames(Key::Space, false, 5);
        CHECK(!q.tracker.WasTapped("Pular", 10.0f));
    }

    SECTION("Segurar");
    {
        q.Frame(Key::Space, true);
        CHECK(Near(q.tracker.HeldTime("Pular"), 0.0f) && !q.tracker.WasHeld("Pular", 0.25f));
        q.Frame(Key::Space, true);
        q.Frame(Key::Space, true);
        CHECK(Near(q.tracker.HeldTime("Pular"), 0.2f) && !q.tracker.WasHeld("Pular", 0.25f));
        q.Frame(Key::Space, true);
        CHECK(q.tracker.WasHeld("Pular", 0.25f));  // passou de 0,25 s neste quadro
        q.Frame(Key::Space, true);
        CHECK(!q.tracker.WasHeld("Pular", 0.25f) && q.tracker.WasHeld("Pular", 0.35f));  // uma vez por limite
        q.Frame(Key::Space, false);
        CHECK(Near(q.tracker.HeldTime("Pular"), 0.0f));
        CHECK(!q.tracker.WasTapped("Pular", 0.25f) && q.tracker.WasTapped("Pular", 1.0f));  // 0,4 s apertada
        q.Frames(Key::Space, false, 5);
    }

    SECTION("Toque e toque duplo");
    {
        q.Frame(Key::Space, true);
        CHECK(!q.tracker.WasDoubleTapped("Pular", 0.3f));  // o anterior foi há muito tempo
        q.Frame(Key::Space, false);
        CHECK(q.tracker.WasTapped("Pular", 0.25f));
        q.Frame(Key::Space, true);  // 0,2 s depois do primeiro
        CHECK(q.tracker.WasDoubleTapped("Pular", 0.3f));
        q.Frame(Key::Space, false);
        q.Frame(Key::Space, true);  // terceiro rápido: começa outra dupla
        CHECK(!q.tracker.WasDoubleTapped("Pular", 0.3f));
        q.Frame(Key::Space, false);
        q.Frame(Key::Space, true);  // quarto: fecha a segunda dupla
        CHECK(q.tracker.WasDoubleTapped("Pular", 0.3f));
        q.Frame(Key::Space, false);
        q.Frames(Key::Space, false, 4);
        q.Frame(Key::Space, true);  // 0,6 s depois: devagar demais
        CHECK(!q.tracker.WasDoubleTapped("Pular", 0.3f));
        q.Frame(Key::Space, false);
        q.Frame(Key::Space, true);  // e este fecha uma dupla com o devagar
        CHECK(q.tracker.WasDoubleTapped("Pular", 0.3f));

        // Toque mais curto que um quadro: aperta e solta entre dois Update.
        q.Frame(Key::Space, false);
        q.Frames(Key::Space, false, 5);
        Input::BeginFrame(kDt);
        Input::SetKeyState(Key::Space, true);
        Input::SetKeyState(Key::Space, false);
        q.tracker.Update(q.actions, q.filter, true, kDt);
        CHECK(q.tracker.ConsumeBuffered("Pular", 0.15f));
        q.Frame(Key::Space, false);
        CHECK(q.tracker.WasTapped("Pular", 0.25f));
        q.Frames(Key::Space, false, 5);
    }

    SECTION("Buffer");
    {
        q.Frame(Key::Space, true);
        q.Frame(Key::Space, false);  // 0,1 s depois do aperto
        CHECK(q.tracker.ConsumeBuffered("Pular", 0.15f));
        CHECK(!q.tracker.ConsumeBuffered("Pular", 0.15f));  // gasto
        q.Frame(Key::Space, true);
        q.Frames(Key::Space, false, 2);  // 0,2 s depois
        CHECK(!q.tracker.ConsumeBuffered("Pular", 0.15f) && q.tracker.ConsumeBuffered("Pular", 0.25f));
        q.Frames(Key::Space, false, 3);
    }

    SECTION("Por jogador, sem foco e contextos");
    {
        CHECK(!q.tracker.ConsumeBuffered("Pular", 1.0f, kKeyboardMouse));  // passa a ser acompanhado agora
        q.Frame(Key::Space, false);
        q.Frame(Key::Space, true);
        CHECK(q.tracker.ConsumeBuffered("Pular", 0.15f, kKeyboardMouse));
        CHECK(!q.tracker.ConsumeBuffered("Pular", 0.15f, 0));  // o controle 0 não apertou nada
        q.Frame(Key::Space, false);
        q.Frames(Key::Space, false, 3);

        q.enabled = false;  // jogo sem foco: solto
        q.Frame(Key::Space, true);
        CHECK(Near(q.tracker.HeldTime("Pular"), 0.0f) && !q.tracker.ConsumeBuffered("Pular", 0.15f));
        q.enabled = true;
        q.Frame(Key::Space, false);
        q.Frames(Key::Space, false, 3);

        const std::vector<std::string> jogo{"Jogo"};
        q.filter.contexts = &jogo;
        q.Frame(Key::M, true);  // "Menu" fora do contexto ativo
        CHECK(!q.tracker.ConsumeBuffered("Menu", 1.0f));
        q.filter.contexts = nullptr;
        q.Frame(Key::M, false);
        q.Frames(Key::M, false, 3);
    }

    SECTION("Scripts: buffer de pulo");
    {
        auto& registry = ComponentRegistry::Instance();
        registry.Register<Pulador>("Pulador", "Pulador");
        Scene scene;
        scene.CreateEntity("P").Add<Pulador>();
        SceneRuntime runtime(scene);  // sem host: o mapa padrão (Pular no Espaço, contexto Jogo)
        runtime.Start();
        Pulador* p = scene.FindByName("P")->Get<Pulador>();
        const auto frame = [&](bool espaco) {
            Input::BeginFrame(kDt);
            Input::SetKeyState(Key::Space, espaco);
            runtime.Update(kDt);
        };
        frame(false);
        frame(true);  // apertou no ar
        CHECK(p->pulos == 0);
        p->noChao = true;  // tocou o chão 0,1 s depois: pula
        frame(false);
        CHECK(p->pulos == 1);
        frame(false);
        CHECK(p->pulos == 1);  // o aperto foi gasto
        p->noChao = false;
        frame(true);
        frame(false);
        frame(false);
        p->noChao = true;  // 0,2 s depois: tarde demais
        frame(false);
        CHECK(p->pulos == 1);
        CHECK(runtime.ActionTracker().HeldTime("Pular") == 0.0f);
        runtime.Stop();
        registry.Unregister(registry.Find("Pulador"));
        Input::BeginFrame();
    }

    return TestResult();
}
