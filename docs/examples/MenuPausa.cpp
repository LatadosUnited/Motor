// Guia de scripts (docs/scripts.md), seção "Ações e controle": menu de pausa com contextos de entrada. A ação
// Pausa (Esc ou Start, no contexto Jogo) mostra o painel "PainelPausa" e troca para o contexto Menu: Pular, Mover
// e as outras do jogo ficam soltas atrás do menu. Lá, Voltar (Esc, B ou Start) ou o botão "Continuar" fecham.
// Ponha numa entidade vazia chamada "Pausa", na cena do jogo.

#include <motor/scene/ScriptModule.h>
#include <motor/ui/UiSystem.h>

struct MenuPausa : motor::Script {
    bool pausado = false;

    void OnStart() override {
        if (auto* ui = GetSystem<motor::UiSystem>()) ui->SetVisible("PainelPausa", false);
    }

    void OnUpdate(float) override {
        auto* ui = GetSystem<motor::UiSystem>();
        if (!pausado && WasActionPressed("Pausa")) {
            AbrirPausa(ui);
        } else if (pausado && (WasActionPressed("Voltar") || (ui && ui->WasClicked("Continuar")))) {
            FecharPausa(ui);
        }
    }

    void AbrirPausa(motor::UiSystem* ui) {
        pausado = true;
        SetInputContext("Menu");  // Pular, Mover... ficam soltos; Navegar, Confirmar e Voltar passam a valer
        if (ui) ui->SetVisible("PainelPausa", true);
    }

    void FecharPausa(motor::UiSystem* ui) {
        pausado = false;
        SetInputContext("Jogo");
        if (ui) ui->SetVisible("PainelPausa", false);
    }
};

MOTOR_SCRIPT(MenuPausa) {
    registry.Register<MenuPausa>("MenuPausa", "Menu de pausa");
}
