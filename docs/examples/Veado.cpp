// Guia de scripts (docs/scripts.md), seção 15 (Navegação), "Ver, ouvir e lembrar": um veado que pasta perto de
// casa e foge quando vê ou ouve o jogador. Os sentidos são do motor (componente "Percepção" no veado, "Alvo de
// percepção" no jogador); o que fazer com eles é deste script. Atrás de uma parede o jogador não é visto, mas os
// passos dele ainda são ouvidos.
// Na cena: uma "Superfície de navegação", os Colliders do cenário e o veado com "Agente de navegação", "Percepção"
// (Percebe os grupos: Jogador) e este script.

#include <motor/ai/NavSystem.h>
#include <motor/ai/PerceptionSystem.h>
#include <motor/scene/ScriptModule.h>

struct Veado : motor::Script {
    float raioPasto = 6.0f;       // m em volta de onde começou
    float pausa = 2.0f;           // s parado pastando entre um passeio e outro
    float distanciaFuga = 8.0f;   // m de cada corrida para longe do perigo
    float velocidadeFuga = 7.0f;  // m/s fugindo
    float calma = 3.0f;           // s sem ver nem ouvir o perigo até voltar a pastar

    void OnStart() override {
        m_casa = glm::vec3(GetScene().WorldMatrix(GetId())[3]);
        if (auto* agente = GetComponent<motor::NavMeshAgent>()) m_velocidadeNormal = agente->speed;
    }

    void OnFixedUpdate(float dt) override {
        auto* nav = GetSystem<motor::NavSystem>();
        auto* sentidos = GetSystem<motor::PerceptionSystem>();
        auto* agente = GetComponent<motor::NavMeshAgent>();
        if (!nav || !sentidos || !agente) return;

        // A memória já junta o que ele vê e o que ouve: o perigo é o mais recente percebido há menos de `calma`
        // segundos, na última posição em que foi visto ou ouvido (a lista vem do mais recente ao mais antigo).
        const std::vector<motor::PerceptionMemory> lembra = sentidos->GetMemory(GetId());
        const bool assustado = !lembra.empty() && lembra.front().age < calma;

        if (assustado) {
            const motor::PerceptionMemory& perigo = lembra.front();
            if (!m_fugindo) Log("o veado {} e foge", perigo.bySight ? "viu alguém" : "ouviu alguma coisa");
            m_fugindo = true;
            agente->speed = velocidadeFuga;
            // Corre para longe e, chegando, corre de novo enquanto o perigo continua perto. Pedir o caminho a cada
            // passo refaria à toa: só quando chega, ou meio segundo depois do último pedido se o perigo foi
            // percebido de novo (ele pode ter andado).
            m_desdeFuga += dt;
            const bool percebeuAgora = perigo.seen || perigo.heard;
            if (!nav->HasPath(GetId()) || (percebeuAgora && m_desdeFuga > 0.5f)) {
                const glm::vec3 eu = motor::NavAgentFeet(GetScene(), GetId());
                if (auto ponto = nav->FleePoint(eu, perigo.position, distanciaFuga)) nav->SetDestination(GetId(), *ponto);
                m_desdeFuga = 0.0f;
            }
            return;
        }
        if (m_fugindo) {  // passou o susto: volta a pastar, devagar
            m_fugindo = false;
            agente->speed = m_velocidadeNormal;
            nav->Stop(GetId());
            m_parado = 0.0f;
        }

        // Pastando: parado um tempo e depois um passeio curto até um ponto ao acaso perto de casa.
        if (nav->HasPath(GetId())) return;
        m_parado += dt;
        if (m_parado < pausa) return;
        if (auto ponto = nav->RandomPoint(m_casa, raioPasto)) nav->SetDestination(GetId(), *ponto);
        m_parado = 0.0f;
    }

    bool Fugindo() const { return m_fugindo; }

private:
    glm::vec3 m_casa{0.0f};
    float m_velocidadeNormal = 3.5f;
    bool m_fugindo = false;
    float m_desdeFuga = 0.0f;  // s desde o último ponto de fuga
    float m_parado = 0.0f;     // s pastando parado
};

MOTOR_SCRIPT(Veado) {
    registry.Register<Veado>("Veado", "Veado (pasta e foge do jogador)")
        .Field("raioPasto", "Raio do pasto", &Veado::raioPasto,
               {.speed = 0.1f, .min = 0.5f, .max = 1000.0f, .format = "%.1f m", .tooltip = "Passeia até aqui de onde começou"})
        .Field("pausa", "Pausa", &Veado::pausa, {.speed = 0.1f, .min = 0.0f, .max = 60.0f, .format = "%.1f s"})
        .Field("distanciaFuga", "Distância da fuga", &Veado::distanciaFuga,
               {.speed = 0.1f, .min = 1.0f, .max = 1000.0f, .format = "%.1f m"})
        .Field("velocidadeFuga", "Velocidade da fuga", &Veado::velocidadeFuga,
               {.speed = 0.1f, .min = 0.1f, .max = 1000.0f, .format = "%.1f m/s"})
        .Field("calma", "Calma depois de", &Veado::calma,
               {.speed = 0.1f, .min = 0.0f, .max = 600.0f, .format = "%.1f s",
                .tooltip = "Sem ver nem ouvir o perigo esse tempo, volta a pastar (até a Memória da Percepção)"});
}
