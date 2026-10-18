// Interruptor e lâmpada: quando o jogador entra na área do interruptor, a lâmpada (escolhida no Inspector com
// um campo EntityRef) recebe a mensagem "ligar", acende e apaga sozinha depois de alguns segundos.
// Mostra: EntityRef, Send/OnMessage, GetComponent e um temporizador (After/CancelTimers).
// Guia: docs/scripts.md, seção 10 (Temporizadores, mensagens e sorteio).

#include <motor/scene/ScriptModule.h>

#include <string>

// Na lâmpada (uma entidade com Mesh Renderer).
struct Lampada : motor::Script {
    glm::vec4 corLigada{1.0f, 0.9f, 0.4f, 1.0f};
    float duracao = 3.0f;  // segundos acesa

    bool ligada = false;  // estado do jogo: não registrado

    void OnStart() override {
        if (auto* mr = GetComponent<motor::MeshRenderer>()) m_corDesligada = mr->material.color;
    }

    // Mensagens de outros scripts chegam aqui. `texto` diz o que aconteceu; `valor` é um número opcional.
    void OnMessage(const motor::Message& mensagem) override {
        if (mensagem.text != "ligar") return;
        Acender(true);
        CancelTimers();  // ligou de novo antes de apagar: recomeça a contagem
        After(duracao, [this] { Acender(false); });
    }

    void Acender(bool acesa) {
        ligada = acesa;
        if (auto* mr = GetComponent<motor::MeshRenderer>()) mr->material.color = acesa ? corLigada : m_corDesligada;
    }

    glm::vec4 m_corDesligada{1.0f};
};

// No interruptor: uma entidade com Collider marcado como Trigger.
struct Interruptor : motor::Script {
    motor::EntityRef lampada;            // arraste a lâmpada da Hierarquia para este campo
    std::string quemAperta = "Jogador";  // nome de quem liga ao entrar na área

    void OnTriggerEnter(motor::EntityId outro) override {
        const motor::Entity* e = GetEntity(outro);
        if (e && e->name == quemAperta) Send(lampada, "ligar");
    }
};

MOTOR_SCRIPT(Lampada) {
    registry.Register<Lampada>("Lampada", "Lâmpada")
        .Field("corLigada", "Cor ligada", &Lampada::corLigada, {.color = true})
        .Field("duracao", &Lampada::duracao, {.speed = 0.1f, .min = 0.1f, .max = 60.0f, .format = "%.1f s"});
}

// Registro curto: o rótulo vem do nome do campo ("quemAperta" aparece como "Quem aperta").
MOTOR_FIELDS(Interruptor, lampada, quemAperta)
