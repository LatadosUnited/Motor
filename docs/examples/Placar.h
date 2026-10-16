// Guia de scripts (docs/scripts.md), receita "Coletável com pontuação": o placar do jogo. Ponha este script
// numa entidade chamada "Placar" (pode ser vazia). As moedas (Moeda.cpp) somam pontos nele. Fica num header
// porque outros scripts o usam; o registro está em Placar.cpp.
#pragma once

#include <motor/scene/ScriptModule.h>

#include <string>

struct Placar : motor::Script {
    int pontos = 0;
    std::string mensagemFinal = "Você pegou todas as moedas!";

    int total = 0;  // pontos possíveis: cada moeda soma o seu valor aqui no OnStart dela

    void Somar(int valor) {
        pontos += valor;
        Log("pontos: {} de {}", pontos, total);
        if (total > 0 && pontos >= total) Log("{}", mensagemFinal);
    }
};

// Acha o placar da cena (a entidade "Placar"), ou nullptr. Use de dentro de outro script:
//   if (Placar* placar = AcharPlacar(GetScene())) placar->Somar(1);
inline Placar* AcharPlacar(motor::Scene& scene) {
    motor::Entity* entidade = scene.FindByName("Placar");
    return entidade ? entidade->Get<Placar>() : nullptr;
}
