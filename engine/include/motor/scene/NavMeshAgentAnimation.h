#pragma once

// Animação do agente de navegação (Fase 35): troca os clipes do Animator (na entidade do agente ou num filho, como o
// modelo glTF) pela velocidade com que o agente anda: parado, andando, correndo, e pulando enquanto atravessa uma
// ligação (NavMeshLink). Só dados: quem troca é o NavSystem (motor/ai/NavSystem.h), a cada passo, com CrossFade.
//
// Com "Acompanhar a velocidade", o Animator toca mais rápido ou mais devagar conforme o agente (andando a 3 m/s um
// clipe feito para 1,5 m/s toca no dobro), para os pés não escorregarem; o campo Velocidade do Animator passa a
// ser do agente. Um script que troque o clipe depois (OnFixedUpdate, OnUpdate) vence até o próximo passo.

#include "motor/scene/Component.h"

#include <string>

namespace motor {

struct NavMeshAgentAnimation {
    std::string idleClip = "Parado";   // nomes dos clipes do modelo do Animator (vazio: não troca nesse caso)
    std::string walkClip = "Andando";
    std::string runClip;               // vazio: só anda, em qualquer velocidade
    std::string jumpClip;              // vazio: atravessa as ligações andando
    float walkSpeed = 1.5f;            // m/s em que o clipe de andar toca na velocidade normal
    float runSpeed = 4.0f;             // m/s em que o de correr toca na velocidade normal
    float idleBelow = 0.1f;            // m/s: abaixo disso, parado
    bool matchSpeed = true;            // o Animator acompanha a velocidade do agente
    float fade = 0.2f;                 // s de mistura entre um clipe e outro

    bool operator==(const NavMeshAgentAnimation&) const = default;
};

} // namespace motor
