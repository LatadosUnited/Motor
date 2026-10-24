// Guia de scripts (docs/scripts.md), seção "Ações e controle": o jogador troca a tecla de pular. O texto
// "TeclaPular" mostra a tecla de agora; o botão "TrocarPular" espera a próxima tecla ou botão apertado e liga no
// lugar da primeira entrada de Pular; "Restaurar" volta às do projeto. No jogo exportado a troca fica gravada
// (controles.json, ao lado das opções); no editor, só neste Play. Ponha numa entidade da tela de opções.

#include <motor/scene/ScriptModule.h>
#include <motor/ui/UiSystem.h>

#include <string>

struct MenuControles : motor::Script {
    bool esperando = false;

    void OnUpdate(float) override {
        auto* ui = GetSystem<motor::UiSystem>();
        if (!ui) return;
        if (esperando) {
            if (auto nova = GetPressedBinding()) {
                for (const std::string& outra : GetBindingConflicts("Pular", *nova)) {
                    Log("aviso: {} já usa {}", outra, nova->Label());
                }
                RebindAction("Pular", 0, *nova);
                esperando = false;
            }
        } else if (ui->WasClicked("TrocarPular")) {
            esperando = true;  // a partir do próximo frame: o clique no botão não vira a tecla
        } else if (ui->WasClicked("Restaurar")) {
            ResetActionBindings();
        }
        const auto teclas = GetActionBindings("Pular");
        ui->SetText("TeclaPular", esperando ? "Aperte uma tecla..." : teclas.empty() ? "-" : teclas[0].Label());
    }
};

MOTOR_SCRIPT(MenuControles) {
    registry.Register<MenuControles>("MenuControles", "Menu de controles");
}
