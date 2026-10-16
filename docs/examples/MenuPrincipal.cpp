// Guia de scripts (docs/scripts.md), seção "Interface do jogo": o menu do jogo. O botão "Jogar" troca para a
// cena do jogo; "Creditos" mostra e esconde o painel "PainelCreditos". Ponha numa entidade vazia chamada
// "Menu", na cena do menu.

#include <motor/scene/ScriptModule.h>
#include <motor/ui/UiSystem.h>

#include <string>

struct MenuPrincipal : motor::Script {
    std::string cenaDoJogo = "Fase1";  // nome da cena (ou caminho: "Assets/Cenas/Fase1.mscene")

    void OnStart() override {
        if (auto* ui = GetSystem<motor::UiSystem>()) ui->SetVisible("PainelCreditos", false);
    }

    void OnUpdate(float) override {
        auto* ui = GetSystem<motor::UiSystem>();
        if (!ui) return;
        if (ui->WasClicked("Jogar")) {
            Log("indo para {}", cenaDoJogo);
            LoadScene(cenaDoJogo);  // no fim deste passo: o menu para e a cena do jogo começa
        }
        if (ui->WasClicked("Creditos")) ui->SetVisible("PainelCreditos", !ui->IsVisible("PainelCreditos"));
    }
};

MOTOR_SCRIPT(MenuPrincipal) {
    registry.Register<MenuPrincipal>("MenuPrincipal", "Menu principal")
        .Field("cenaDoJogo", "Cena do jogo", &MenuPrincipal::cenaDoJogo);
}
