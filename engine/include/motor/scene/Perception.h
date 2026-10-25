#pragma once

// Percepção (Fase 37): os sentidos de um agente. Ele vê quem tem PerceptionTarget dentro do cone (ou bem perto, em
// qualquer direção) e sem nada no meio, ouve os ruídos que chegam até ele e lembra por um tempo onde viu ou ouviu
// cada um. Só dados: quem percebe é o PerceptionSystem (motor/ai/PerceptionSystem.h), antes dos scripts; o que o
// agente faz com isso (fugir, perseguir) é do script dele.
//
// Os olhos ficam `eyeHeight` acima da origem da entidade e olham para a frente dela (-Z), girando com ela.

#include "motor/scene/Component.h"

#include <string>
#include <string_view>

namespace motor {

struct Perception {
    float sightRange = 15.0f;   // m: até onde vê (vezes a visibilidade do alvo); 0 = cego
    float sightAngle = 120.0f;  // graus: abertura inteira do cone de visão
    float nearRange = 1.5f;     // m: percebe em qualquer direção (atrás também), se nada tapar
    float eyeHeight = 1.6f;     // m: altura dos olhos acima da origem da entidade
    float hearing = 1.0f;       // multiplica o alcance dos ruídos que chegam a ele; 0 = surdo
    float memory = 5.0f;        // s: quanto lembra de um alvo que não percebe mais
    // Grupos de alvos que percebe (PerceptionTarget::group), separados por vírgula: "Jogador, Lobo". Vazio: todos.
    std::string detects;

    // Percebe alvos do grupo `group`? Compara cada nome de `detects` sem os espaços em volta.
    bool Detects(std::string_view group) const {
        std::string_view rest = detects;
        bool any = false;
        while (!rest.empty()) {
            const size_t comma = rest.find(',');
            std::string_view name = rest.substr(0, comma);
            rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
            while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
            while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
            if (name.empty()) continue;
            if (name == group) return true;
            any = true;
        }
        return !any;
    }

    bool operator==(const Perception&) const = default;
};

} // namespace motor
