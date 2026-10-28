// Guia de scripts (docs/scripts.md), seção "Menus em documento de UI": o menu do jogo feito num documento (.mui) com
// consultas e eventos, sem ler botão por botão no OnUpdate. Elementos do documento: os botões "#jogar" e "#sair"
// (classe "menu"), o slider "#volume", a caixa de seleção "#musica" e o campo de texto "#nome". Ponha na entidade
// do documento (UI: Documento), na cena do menu. Funciona com o mouse, com o teclado (setas, Enter, Esc) e com o
// controle (direcional, A, B): o contexto de entrada Menu liga a navegação do foco.

#include <motor/scene/ScriptModule.h>
#include <motor/ui/UiDocument.h>
#include <motor/ui/UiSystem.h>

#include <format>
#include <string>

struct MenuDocumento : motor::Script {
    std::string cenaDoJogo = "Fase1";
    float volume = 0.8f;
    bool musica = true;
    std::string jogador;

    void OnStart() override {
        auto* ui = GetSystem<motor::UiSystem>();
        if (!ui) return;
        SetInputContext("Menu");  // setas e direcional movem o foco; Confirmar (Enter, A) clica
        ui->Focus("#jogar");

        // Valores iniciais nos controles, e os eventos. Os callbacks são deste script: somem sozinhos ao parar o
        // Play, ao destruir a entidade e na recarga dos scripts (registre de novo no OnStart, como aqui).
        if (motor::UiElement* slider = ui->Q("#volume")) slider->SetValue(volume);
        if (motor::UiElement* caixa = ui->Q("#musica")) caixa->checked = musica;
        ui->OnClick("#jogar", [this] { LoadScene(cenaDoJogo); });
        ui->OnClick("#sair", [this] { QuitGame(); });
        ui->OnChange("#volume", [this](motor::UiElement& e) {
            volume = e.value;
            if (auto* texto = GetSystem<motor::UiSystem>()->Q("#volume_texto")) {
                texto->text = std::format("Volume: {:.0f}%", volume * 100.0f);
            }
        });
        ui->OnChange("#musica", [this](motor::UiElement& e) { musica = e.checked; });
        ui->OnClick("#nome", [this](motor::UiElement& e) {  // Enter no campo de texto
            jogador = e.text;
            Log("jogador: {}", jogador);
        });
        // Destaque de passagem: o botão sob o mouse ganha um "> " na frente.
        ui->OnHover(".menu", [](motor::UiElement& e, bool entrou) {
            const bool marcado = e.text.starts_with("> ");
            if (entrou && !marcado) e.text = "> " + e.text;
            if (!entrou && marcado) e.text = e.text.substr(2);
        });
    }
};

MOTOR_SCRIPT(MenuDocumento) {
    registry.Register<MenuDocumento>("MenuDocumento", "Menu em documento")
        .Field("cenaDoJogo", "Cena do jogo", &MenuDocumento::cenaDoJogo)
        .Field("volume", "Volume", &MenuDocumento::volume)
        .Field("musica", "Música", &MenuDocumento::musica)
        .Field("jogador", "Jogador", &MenuDocumento::jogador);
}
