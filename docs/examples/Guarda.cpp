// Guia de scripts (docs/scripts.md), seção 15 (Navegação): um guarda que patrulha entre dois pontos pelo navmesh e
// persegue o alvo quando o vê; quando o perde de vista, volta a patrulhar.
// Na entidade do guarda: Mesh Renderer, "Agente de navegação" e este script. Na cena: uma "Superfície de
// navegação" e os Colliders do cenário (chão, paredes), e duas entidades vazias para os pontos da patrulha.

#include <motor/ai/NavSystem.h>
#include <motor/scene/ScriptModule.h>

#include <glm/geometric.hpp>

struct Guarda : motor::Script {
    motor::EntityRef pontoA, pontoB;  // patrulha de um ao outro
    motor::EntityRef alvo;            // quem ele persegue (o jogador)
    float visao = 8.0f;               // até onde enxerga, em metros

    void OnFixedUpdate(float /*dt*/) override {
        auto* nav = GetSystem<motor::NavSystem>();
        if (!nav) return;
        const glm::vec3 eu = Posicao(GetId());

        // Vê o alvo se ele está perto e o caminho reto até ele, pelo navmesh, não bate em parede.
        if (GetEntity(alvo)) {
            const glm::vec3 ele = Posicao(alvo);
            const bool perto = glm::distance(eu, ele) < visao;
            if (perto && !nav->Raycast(eu, ele)) {
                // Refaz o caminho só quando o alvo andou: pedir a cada passo refaria à toa.
                if (!m_perseguindo || glm::distance(ele, m_ultimoVisto) > 0.5f) {
                    nav->SetDestination(GetId(), ele);
                    m_ultimoVisto = ele;
                }
                m_perseguindo = true;
                return;
            }
        }

        // Patrulha: chegou num ponto (ou acabou de perder o alvo de vista), vai para o outro.
        if (m_perseguindo || nav->HasArrived(GetId()) || !nav->HasPath(GetId())) {
            m_indoParaB = m_perseguindo ? m_indoParaB : !m_indoParaB;
            m_perseguindo = false;
            const motor::EntityRef ponto = m_indoParaB ? pontoB : pontoA;
            if (GetEntity(ponto)) nav->SetDestination(GetId(), Posicao(ponto));
        }
    }

private:
    glm::vec3 Posicao(motor::EntityId id) { return glm::vec3(GetScene().WorldMatrix(id)[3]); }

    bool m_perseguindo = false;
    bool m_indoParaB = false;
    glm::vec3 m_ultimoVisto{0.0f};
};

MOTOR_SCRIPT(Guarda) {
    registry.Register<Guarda>("Guarda", "Guarda (patrulha e persegue)")
        .Field("pontoA", "Ponto A", &Guarda::pontoA, {.tooltip = "Um dos dois pontos da patrulha"})
        .Field("pontoB", "Ponto B", &Guarda::pontoB)
        .Field("alvo", "Alvo", &Guarda::alvo, {.tooltip = "Quem o guarda persegue quando vê"})
        .Field("visao", "Visão", &Guarda::visao, {.speed = 0.1f, .min = 0.0f, .max = 1000.0f, .format = "%.1f m"});
}
