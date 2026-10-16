// Guia de scripts (docs/scripts.md), receita "Porta com trigger": a porta sobe quando o jogador entra na área
// do sensor e desce quando ele sai.
// Duas entidades:
//   "Porta"   Mesh Renderer + Collider + Rigidbody com "Cinemático" marcado (segue o Transform e empurra).
//   sensor    Collider com "Trigger" marcado, maior que a passagem, na frente da porta, e este script.

#include <motor/scene/ScriptModule.h>

#include <algorithm>
#include <string>

struct Porta : motor::Script {
    std::string porta = "Porta";  // nome da entidade que se mexe
    float altura = 3.0f;          // quanto sobe, em metros
    float velocidade = 2.0f;      // metros por segundo
    bool fecharAoSair = true;

    void OnStart() override {
        motor::Entity* entidade = FindEntity(porta);
        if (!entidade) {
            Log("porta \"{}\" não encontrada", porta);
            return;
        }
        m_porta = entidade->id;
        m_fechadaY = entidade->transform.position.y;
    }

    void OnTriggerEnter(motor::EntityId outro) override {
        if (EhJogador(outro)) ++m_dentro;
    }

    void OnTriggerExit(motor::EntityId outro) override {
        if (EhJogador(outro) && m_dentro > 0) --m_dentro;
    }

    void OnUpdate(float dt) override {
        motor::Entity* entidade = GetScene().Find(m_porta);  // pode ter sido destruída
        if (!entidade) return;
        const bool aberta = m_dentro > 0 || (!fecharAoSair && m_abriu);
        if (m_dentro > 0) m_abriu = true;

        // Anda até o alvo sem passar dele.
        float& y = entidade->transform.position.y;
        const float alvo = aberta ? m_fechadaY + altura : m_fechadaY;
        const float passo = velocidade * dt;
        y = std::clamp(alvo, y - passo, y + passo);
    }

private:
    bool EhJogador(motor::EntityId id) {
        const motor::Entity* entidade = GetScene().Find(id);
        return entidade && entidade->Has<motor::CharacterControllerComponent>();
    }

    motor::EntityId m_porta = motor::kNullEntity;  // guarde ids, não ponteiros
    float m_fechadaY = 0.0f;
    int m_dentro = 0;  // jogadores dentro do sensor
    bool m_abriu = false;
};

MOTOR_SCRIPT(Porta) {
    registry.Register<Porta>("Porta", "Porta com sensor")
        .Field("porta", "Porta", &Porta::porta, {.tooltip = "Nome da entidade que sobe e desce"})
        .Field("altura", "Altura", &Porta::altura, {.speed = 0.05f, .min = 0.0f, .max = 100.0f, .format = "%.2f m"})
        .Field("velocidade", "Velocidade", &Porta::velocidade, {.speed = 0.05f, .min = 0.01f, .max = 100.0f, .format = "%.1f m/s"})
        .Field("fecharAoSair", "Fechar ao sair", &Porta::fecharAoSair);
}
