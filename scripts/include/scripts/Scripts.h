#pragma once

// Scripts de jogo compilados junto com o editor (biblioteca MotorScripts). Para criar um script novo:
// declare a struct aqui (herdando de motor::Script), implemente num .cpp desta pasta, registre em
// Scripts.cpp e adicione o .cpp em scripts/CMakeLists.txt.

#include <motor/scene/Script.h>

#include <glm/vec3.hpp>

#include <string>

namespace motor {
class ComponentRegistry;
}

namespace scripts {

// Registra todos os scripts abaixo (pode ser chamada mais de uma vez).
void RegisterAll(motor::ComponentRegistry& registry);

// Gira a entidade continuamente.
struct Girar : motor::Script {
    glm::vec3 eixo{0.0f, 1.0f, 0.0f};
    float velocidade = 90.0f;  // graus por segundo
    bool local = true;         // eixo do próprio objeto (senão, do mundo)

    void OnUpdate(float dt) override;
};

// Move a entidade no plano XZ com WASD (Shift corre). W = -Z (para "frente" no mundo).
struct MoverWasd : motor::Script {
    float velocidade = 4.0f;  // metros por segundo
    float corrida = 2.0f;     // multiplicador com Shift
    bool virar = true;        // vira a frente (-Z) para a direção do movimento

    void OnUpdate(float dt) override;
};

// Segue outra entidade (achada pelo nome) mantendo um deslocamento; útil na câmera. A entidade que segue
// deve estar na raiz da hierarquia.
struct Seguir : motor::Script {
    std::string alvo = "Jogador";
    glm::vec3 deslocamento{0.0f, 4.0f, 7.0f};
    float suavidade = 6.0f;  // 0 = instantâneo
    bool olhar = true;       // aponta para o alvo

    void OnStart() override;
    void OnUpdate(float dt) override;

private:
    motor::EntityId m_alvo = 0;  // estado interno: não registrado, não é salvo
};

} // namespace scripts
