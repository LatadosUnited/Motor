// Guia de scripts (docs/scripts.md), seção "Salvar o progresso": menu com "Continuar" e "Novo jogo". Continuar só
// aparece se houver um save; Novo jogo apaga o save e começa da primeira fase. Na cena do jogo, o script
// SalvarFase (abaixo) grava a fase e a posição do jogador ao chegar. Botões: "Continuar" e "NovoJogo". Ponha numa
// entidade vazia chamada "Menu".

#include <motor/scene/ScriptModule.h>
#include <motor/ui/UiSystem.h>

#include <string>

struct MenuContinuar : motor::Script {
    std::string primeiraFase = "Fase1";

    void OnStart() override {
        if (auto* ui = GetSystem<motor::UiSystem>()) ui->SetVisible("Continuar", HasSave());
    }

    void OnUpdate(float) override {
        auto* ui = GetSystem<motor::UiSystem>();
        if (!ui) return;
        if (ui->WasClicked("Continuar")) {
            LoadScene(SaveSlot().GetString("fase", primeiraFase));
        } else if (ui->WasClicked("NovoJogo")) {
            DeleteSave();  // começa do zero
            LoadScene(primeiraFase);
        }
    }
};

MOTOR_SCRIPT(MenuContinuar) {
    registry.Register<MenuContinuar>("MenuContinuar", "Menu com continuar")
        .Field("primeiraFase", "Primeira fase", &MenuContinuar::primeiraFase);
}

// Na entidade do jogador de cada fase: grava em que fase está e, ao continuar, volta para onde parou.
struct SalvarFase : motor::Script {
    std::string fase = "Fase1";  // o nome desta cena

    void OnStart() override {
        motor::SaveData& save = SaveSlot();
        if (save.GetString("fase") == fase && save.Has("posicao")) {
            GetTransform().position = save.GetVec3("posicao");  // continuar: de onde parou
        }
        save.SetString("fase", fase);
        save.SetInt("vezes", save.GetInt("vezes", 0) + 1);
        save.Save();
    }

    // A posição muda o tempo todo: grava a cada 5 segundos (e o jogo grava o que faltar ao fechar).
    float relogio = 0.0f;
    void OnUpdate(float dt) override {
        relogio += dt;
        if (relogio < 5.0f) return;
        relogio = 0.0f;
        SaveSlot().SetVec3("posicao", GetTransform().position);
        SaveSlot().Save();
    }
};

MOTOR_SCRIPT(SalvarFase) {
    registry.Register<SalvarFase>("SalvarFase", "Salvar fase").Field("fase", "Fase", &SalvarFase::fase);
}
