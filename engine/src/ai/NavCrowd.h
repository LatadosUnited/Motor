#pragma once

// Multidão do DetourCrowd sobre um NavMesh (só dentro da camada ai, Fase 35). Quem anda, desvia dos outros agentes
// e refaz caminhos é o DetourCrowd; o NavSystem diz quem são os agentes, para onde vão e põe o resultado no
// Transform. Cada agente é um índice da multidão (de 0 a Capacity() - 1).
//
// Caminhos: SetDestination busca na hora (o NavSystem responde se há caminho, como na Fase 34) e entrega o caminho
// pronto à multidão; RequestDestination entra na fila do DetourCrowd, que divide as buscas entre os quadros (quem
// segue um alvo e as buscas que a própria multidão refaz quando o caminho fica inválido).

#include <glm/vec3.hpp>

#include <memory>
#include <vector>

class dtCrowd;

namespace motor {

class NavMesh;

class NavCrowd {
public:
    struct AgentParams {
        float radius = 0.5f;
        float height = 2.0f;
        float speed = 3.5f;
        float acceleration = 8.0f;
        bool avoidOthers = true;  // desvia dos outros agentes (sem isso, só não atravessa)
    };

    // Raio máximo de um agente: maior que isso conta como este (a grade de vizinhos da multidão usa esse tamanho).
    static constexpr float kMaxAgentRadius = 2.0f;

    // nullptr se o DetourCrowd recusar o navmesh. O navmesh tem de viver mais que a multidão.
    static std::unique_ptr<NavCrowd> Create(const NavMesh& navMesh, int capacity);
    ~NavCrowd();
    NavCrowd(const NavCrowd&) = delete;
    NavCrowd& operator=(const NavCrowd&) = delete;

    int Capacity() const { return m_capacity; }
    // Põe um agente em `position` (levado ao navmesh). -1 se a multidão está cheia.
    int Add(const glm::vec3& position, const AgentParams& params);
    void Remove(int index);
    void SetParams(int index, const AgentParams& params);

    // Caminho buscado na hora até `destination` (levado ao navmesh). false se o agente ou o destino estão longe do
    // navmesh; destino em outra ilha vale como caminho até o ponto mais perto dele (`partial`).
    bool SetDestination(int index, const glm::vec3& destination, bool* partial = nullptr);
    // O mesmo, pela fila (o caminho chega em alguns passos). false se o destino está longe do navmesh.
    bool RequestDestination(int index, const glm::vec3& destination);
    void Stop(int index);  // esquece o destino: a multidão freia o agente
    // Põe o agente onde o corpo físico dele está, pelo navmesh (fica preso nele se o corpo saiu). false se está
    // longe demais para ir pelo corredor (teleporte): quem chama tira e põe o agente de novo.
    bool MoveTo(int index, const glm::vec3& feet);

    void Update(float dt);

    bool OnNavMesh(int index) const;
    bool PathPending(int index) const;  // pedido na fila, sem caminho ainda
    bool HasPath(int index) const;      // tem caminho válido até um destino
    bool IsPartial(int index) const;
    glm::vec3 Position(int index) const;  // os pés no navmesh
    glm::vec3 Velocity(int index) const;
    // O que falta até o destino pelos próximos cantos que a multidão conhece; `known` fica false quando o fim do
    // caminho está além deles (o que falta é mais que isso).
    float RemainingNear(int index, bool* known) const;
    // Encostado em outro agente (com folga de um quarto da soma dos raios) no último Update; `idleOnly`: só conta
    // agente parado, sem destino.
    bool Touching(int index, bool idleOnly) const;
    // Cantos do caminho inteiro, da posição do agente até o destino (vazio sem caminho).
    std::vector<glm::vec3> Corners(int index) const;

private:
    NavCrowd() = default;
    void SeparateOverlaps();

    static constexpr unsigned char kTouching = 1;
    static constexpr unsigned char kTouchingIdle = 2;

    const NavMesh* m_navMesh = nullptr;
    dtCrowd* m_crowd = nullptr;
    int m_capacity = 0;
    std::vector<unsigned char> m_touching;  // por índice: kTouching, kTouchingIdle
};

} // namespace motor
