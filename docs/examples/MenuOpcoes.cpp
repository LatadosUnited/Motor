// Guia de scripts (docs/scripts.md), seção "Interface do jogo": menu de opções do jogador. Cada botão troca uma
// opção e aplica na hora; o texto do botão mostra o valor atual. No jogo exportado as opções ficam gravadas e o
// jogo abre com elas da próxima vez. Botões (UI: Botão com um UI: Texto filho de mesmo nome + "Texto"):
// "TelaCheia", "Resolucao", "Vsync", "Sombras", "Sair". Ponha numa entidade vazia chamada "Opcoes".

#include <motor/scene/ScriptModule.h>
#include <motor/ui/UiSystem.h>

#include <format>
#include <string>
#include <vector>

struct MenuOpcoes : motor::Script {
    void OnStart() override { Mostrar(); }

    void OnUpdate(float) override {
        auto* ui = GetSystem<motor::UiSystem>();
        if (!ui) return;
        motor::GameOptions o = GetOptions();
        bool mudou = true;
        if (ui->WasClicked("TelaCheia")) {
            o.fullscreen = !o.fullscreen;
        } else if (ui->WasClicked("Resolucao")) {
            // Próxima resolução da lista do monitor (depois da maior, volta ao padrão: 0 x 0).
            const std::vector<motor::Resolution> lista = Resolutions();
            size_t proxima = 0;
            for (size_t i = 0; i < lista.size(); ++i) {
                if (lista[i].width == o.width && lista[i].height == o.height) proxima = i + 1;
            }
            const motor::Resolution r = proxima < lista.size() ? lista[proxima] : motor::Resolution{};
            o.width = r.width;
            o.height = r.height;
        } else if (ui->WasClicked("Vsync")) {
            o.vsync = !o.vsync;
        } else if (ui->WasClicked("Sombras")) {
            o.shadows = static_cast<motor::ShadowSetting>((static_cast<int>(o.shadows) + 1) % 4);  // desligadas..altas
        } else if (ui->WasClicked("Sair")) {
            QuitGame();
            mudou = false;
        } else {
            mudou = false;
        }
        if (mudou) {
            ApplyOptions(o);
            Mostrar();
        }
    }

    void Mostrar() {
        auto* ui = GetSystem<motor::UiSystem>();
        if (!ui) return;
        const motor::GameOptions o = GetOptions();
        static constexpr const char* kSombras[] = {"desligadas", "baixas", "médias", "altas"};
        ui->SetText("TelaCheiaTexto", o.fullscreen ? "Tela cheia: sim" : "Tela cheia: não");
        ui->SetText("ResolucaoTexto", o.width == 0 ? std::string("Resolução: padrão")
                                                   : std::format("Resolução: {}x{}", o.width, o.height));
        ui->SetText("VsyncTexto", o.vsync ? "Vsync: ligado" : "Vsync: desligado");
        ui->SetText("SombrasTexto", std::format("Sombras: {}", kSombras[static_cast<int>(o.shadows)]));
    }
};

MOTOR_SCRIPT(MenuOpcoes) {
    registry.Register<MenuOpcoes>("MenuOpcoes", "Menu de opções");
}
