// Guia de scripts (docs/scripts.md), seção "Interface do jogo": o HUD do mini jogo. Mostra a vida e os pontos
// do Placar (Placar.h) nos textos "Vida" e "Pontos", e o painel "FimDeJogo" quando a vida acaba. O jogador
// que cai do chão volta ao começo e perde uma vida. Ponha numa entidade vazia chamada "HUD".

#include "Placar.h"

#include <motor/scene/ScriptModule.h>
#include <motor/ui/UiSystem.h>

#include <format>
#include <string>

struct Hud : motor::Script {
    int vida = 3;
    float alturaMinima = -5.0f;  // abaixo disso o jogador caiu
    std::string jogador = "Jogador";

    glm::vec3 m_inicio{0.0f};

    void OnStart() override {
        if (motor::Entity* j = FindEntity(jogador)) m_inicio = j->transform.position;
        if (auto* ui = GetSystem<motor::UiSystem>()) ui->SetVisible("FimDeJogo", false);
    }

    void OnUpdate(float) override {
        auto* ui = GetSystem<motor::UiSystem>();
        if (!ui) return;

        motor::Entity* j = FindEntity(jogador);
        if (j && vida > 0 && j->transform.position.y < alturaMinima) {
            j->transform.position = m_inicio;  // de volta ao começo
            --vida;
            if (vida == 0) ui->SetVisible("FimDeJogo", true);
        }

        // Mudar o texto todo frame é barato: se ficou igual, a geometria do texto não é refeita.
        const Placar* placar = AcharPlacar(GetScene());
        ui->SetText("Vida", std::format("Vida: {}", vida));
        ui->SetText("Pontos", std::format("Pontos: {}", placar ? placar->pontos : 0));
    }
};

MOTOR_SCRIPT(Hud) {
    registry.Register<Hud>("Hud", "HUD (vida e pontos)")
        .Field("vida", "Vida", &Hud::vida, {.min = 0.0f, .max = 99.0f})
        .Field("alturaMinima", "Altura mínima", &Hud::alturaMinima)
        .Field("jogador", "Jogador", &Hud::jogador);
}
