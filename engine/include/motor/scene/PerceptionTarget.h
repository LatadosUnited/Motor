#pragma once

// Alvo de percepção (Fase 37): uma entidade que os agentes com Perception podem ver e ouvir (o jogador, uma ovelha,
// uma isca). Sem este componente ninguém a percebe, mesmo que esteja na frente. Só dados: quem lê é o
// PerceptionSystem (motor/ai/PerceptionSystem.h).
//
// A linha de visada vai dos olhos de quem olha até `height` acima da origem do alvo; um Collider no caminho (que
// não seja do próprio alvo) tapa a visão.

#include "motor/scene/Component.h"

#include <string>

namespace motor {

struct PerceptionTarget {
    std::string group = "Jogador";  // os agentes escolhem quais grupos percebem (Perception::detects)
    float height = 1.0f;            // m: ponto olhado acima da origem (o meio do corpo)
    float visibility = 1.0f;        // multiplica o alcance da visão de quem olha (agachado, no escuro); 0 = invisível
    // m: até onde os sons do AudioSource desta entidade chegam aos agentes (vezes o volume da fonte); 0 = não chegam
    float soundRange = 10.0f;

    bool operator==(const PerceptionTarget&) const = default;
};

} // namespace motor
