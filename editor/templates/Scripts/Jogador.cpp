// Jogador com física: anda com WASD ou o analógico (Shift ou apertar o analógico corre) e pula com Espaço ou o
// botão A, sem atravessar o chão nem as paredes. Usa as ações do projeto (Arquivo > Controles): Horizontal,
// Vertical, Pular e Correr.
// Precisa do componente "Controlador de personagem" na mesma entidade; o chão e as paredes precisam de Collider.
// W = -Z ("frente" no mundo). Script de exemplo do Motor.
// Com um modelo animado (glTF com Animator) na entidade ou como filho, troca entre as animações parado,
// andando e pulando (nomes nos campos; o Robo.glb de exemplo usa "Parado", "Andando" e "Pulando").
// Guia passo a passo: docs/scripts.md na pasta do Motor (seções "Física" e "Animação", receita "Jogador").

#include <motor/physics/PhysicsSystem.h>
#include <motor/scene/Animator.h>
#include <motor/scene/ScriptModule.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>

struct Jogador : motor::Script {
    float velocidade = 4.0f;  // metros por segundo
    float corrida = 2.0f;     // multiplicador ao correr
    float pulo = 5.0f;        // velocidade para cima ao pular (m/s): ~1.3 m de altura
    bool virar = true;        // vira a frente (-Z) para a direção do movimento
    std::string animParado = "Parado";    // clipes do Animator (vazio = não troca)
    std::string animAndando = "Andando";
    std::string animPulando = "Pulando";
    float transicao = 0.15f;  // segundos de mistura entre as animações

    bool querPular = false;  // não registrado: estado interno do Play

    // O botão é lido a cada frame (OnUpdate) e o pulo vai no passo da física (OnFixedUpdate): assim nenhum
    // aperto de Pular se perde entre dois passos.
    void OnUpdate(float) override {
        if (WasActionPressed("Pular")) querPular = true;
    }

    void OnFixedUpdate(float) override {
        // A física é um sistema do jogo: existe só no Play (fora dele, nullptr).
        auto* fisica = GetSystem<motor::PhysicsSystem>();
        if (!fisica) return;

        // Vertical para cima (W, analógico para a frente) = -Z.
        glm::vec3 dir{GetAxis("Horizontal"), 0.0f, -GetAxis("Vertical")};
        const bool moving = glm::length(dir) > 1e-3f;
        // Na diagonal do teclado não anda mais rápido; o analógico meio inclinado anda mais devagar.
        if (glm::length(dir) > 1.0f) dir = glm::normalize(dir);

        const bool running = IsActionDown("Correr");
        // MoveCharacter vale até a próxima chamada: chamar a cada passo (com zero, para).
        fisica->MoveCharacter(GetId(), dir * velocidade * (running ? corrida : 1.0f));
        bool pulou = false;
        if (querPular) {
            pulou = fisica->Jump(GetId(), pulo);  // só pula se estiver no chão
            querPular = false;
        }

        // Frente do objeto é -Z: o ângulo em Y que leva -Z até dir.
        if (virar && moving) {
            GetTransform().rotation = glm::angleAxis(std::atan2(-dir.x, -dir.z), glm::vec3(0.0f, 1.0f, 0.0f));
        }

        // Animação: CrossFade com o clipe que já está tocando não faz nada, então dá para pedir a cada passo.
        if (auto* anim = GetAnimator()) {
            const bool noAr = pulou || !fisica->IsGrounded(GetId());
            const std::string& clipe = noAr ? animPulando : (moving ? animAndando : animParado);
            if (!clipe.empty()) anim->CrossFade(clipe, transicao, !noAr);  // o pulo não repete: segura a pose
            anim->speed = moving && running && !noAr ? corrida : 1.0f;
        }
    }
};

MOTOR_SCRIPT(Jogador) {
    registry.Register<Jogador>("Jogador", "Jogador (física)")
        .Field("velocidade", "Velocidade", &Jogador::velocidade,
               {.speed = 0.1f, .min = 0.0f, .max = 100.0f, .format = "%.1f m/s"})
        .Field("corrida", "Corrida", &Jogador::corrida,
               {.speed = 0.05f, .min = 1.0f, .max = 10.0f, .format = "x%.1f", .tooltip = "Multiplicador ao correr (ação Correr)"})
        .Field("pulo", "Pulo", &Jogador::pulo,
               {.speed = 0.05f, .min = 0.0f, .max = 50.0f, .format = "%.1f m/s", .tooltip = "Velocidade para cima ao pular"})
        .Field("virar", "Virar", &Jogador::virar, {.tooltip = "Vira o objeto para a direção do movimento"})
        .Field("animParado", "Anim. parado", &Jogador::animParado, {.tooltip = "Clipe do Animator (vazio = não troca)"})
        .Field("animAndando", "Anim. andando", &Jogador::animAndando)
        .Field("animPulando", "Anim. pulando", &Jogador::animPulando)
        .Field("transicao", "Transição", &Jogador::transicao,
               {.speed = 0.01f, .min = 0.0f, .max = 2.0f, .format = "%.2f s", .tooltip = "Mistura entre as animações"});
}
