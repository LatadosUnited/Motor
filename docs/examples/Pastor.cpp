// Guia de scripts (docs/scripts.md), seção 15 (Navegação), "Multidão, pulos e lama": um pastor que leva o rebanho
// até o curral. As ovelhas são filhas do pastor, cada uma com "Agente de navegação"; elas desviam umas das outras
// sozinhas (multidão), e cada uma vai para um lugar em volta do curral, para não se espremerem num ponto só. Com
// chuva, a lama fica cara e o rebanho dá a volta por ela; sem chuva, atravessa.
// Na cena: uma "Superfície de navegação", os Colliders do cenário, uma entidade vazia para o curral e uma "Área de
// navegação" na lama.

#include <motor/ai/NavSystem.h>
#include <motor/scene/ScriptModule.h>

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <vector>

struct Pastor : motor::Script {
    motor::EntityRef curral;      // para onde o rebanho vai
    motor::EntityRef lama;        // entidade com a Área de navegação da lama
    bool chovendo = false;
    float custoComChuva = 6.0f;   // andar 1 m na lama molhada conta como 6 m
    float espaco = 1.2f;          // m entre as ovelhas em volta do curral

    void OnStart() override {
        if (auto* area = GetComponent<motor::NavMeshArea>(lama)) m_custoSeco = area->cost;
        Recolher();
    }

    void OnFixedUpdate(float /*dt*/) override {
        auto* nav = GetSystem<motor::NavSystem>();
        if (!nav) return;

        // O custo vale na hora, sem gerar o navmesh de novo: as ovelhas que ainda vão pedir caminho já veem a lama
        // nova. As que estão andando continuam no caminho que têm; Recolher() refaz o de todas.
        if (auto* area = GetComponent<motor::NavMeshArea>(lama)) {
            const float custo = chovendo ? custoComChuva : m_custoSeco;
            if (area->cost != custo) {
                area->cost = custo;
                Recolher();
            }
        }

        // Todas chegaram (cada uma no seu lugar, ou o mais perto que deu): avisa uma vez.
        int chegaram = 0;
        for (motor::EntityId ovelha : m_ovelhas) {
            if (nav->HasArrived(ovelha)) ++chegaram;
            if (nav->IsOnLink(ovelha) && !m_pulou) {
                m_pulou = true;
                Log("uma ovelha pulou a cerca");
            }
        }
        if (!m_avisou && !m_ovelhas.empty() && chegaram == static_cast<int>(m_ovelhas.size())) {
            m_avisou = true;
            Log("rebanho no curral ({} ovelhas)", chegaram);
        }
    }

    // Manda cada ovelha (filha do pastor com Agente de navegação) para um lugar num círculo em volta do curral.
    void Recolher() {
        auto* nav = GetSystem<motor::NavSystem>();
        if (!nav || !GetEntity(curral)) return;
        m_ovelhas.clear();
        for (motor::EntityId filho : GetScene().Children(GetId())) {
            if (GetComponent<motor::NavMeshAgent>(filho)) m_ovelhas.push_back(filho);
        }
        const glm::vec3 centro = glm::vec3(GetScene().WorldMatrix(curral)[3]);
        const float n = static_cast<float>(m_ovelhas.size());
        const float raio = n > 1.0f ? espaco * n / glm::two_pi<float>() : 0.0f;  // a volta cabe todas
        for (size_t i = 0; i < m_ovelhas.size(); ++i) {
            const float angulo = glm::two_pi<float>() * static_cast<float>(i) / std::max(n, 1.0f);
            nav->SetDestination(m_ovelhas[i], centro + raio * glm::vec3(std::cos(angulo), 0.0f, std::sin(angulo)));
        }
        m_avisou = false;
    }

private:
    std::vector<motor::EntityId> m_ovelhas;
    float m_custoSeco = 1.0f;
    bool m_avisou = false;
    bool m_pulou = false;
};

MOTOR_SCRIPT(Pastor) {
    registry.Register<Pastor>("Pastor", "Pastor (leva o rebanho ao curral)")
        .Field("curral", "Curral", &Pastor::curral, {.tooltip = "Para onde o rebanho vai"})
        .Field("lama", "Lama", &Pastor::lama, {.tooltip = "Entidade com a Área de navegação da lama"})
        .Field("chovendo", "Chovendo", &Pastor::chovendo, {.tooltip = "Com chuva a lama fica cara e o rebanho dá a volta"})
        .Field("custoComChuva", "Custo com chuva", &Pastor::custoComChuva,
               {.speed = 0.1f, .min = 1.0f, .max = 1000.0f, .format = "%.1f"})
        .Field("espaco", "Espaço", &Pastor::espaco, {.speed = 0.05f, .min = 0.1f, .max = 100.0f, .format = "%.2f m"});
}
