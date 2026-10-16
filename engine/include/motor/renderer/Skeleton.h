#pragma once

// Esqueleto e pele (skinning) das malhas animadas (Fase 11). Só dados e contas na CPU: o renderer recebe as
// matrizes das juntas prontas (Renderer::SubmitSkinned) e o shader faz o resto. As mesmas contas na CPU
// (SkinPosition) servem para os testes.

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

// Posição, rotação e escala de uma junta em relação à junta pai.
struct JointTransform {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    glm::mat4 Matrix() const;  // T * R * S
};

// Mistura de duas poses de junta: t = 0 dá `a`, t = 1 dá `b` (rotação pelo caminho mais curto).
JointTransform Blend(const JointTransform& a, const JointTransform& b, float t);

// Hierarquia de juntas de um modelo. No glTF importado, todas as juntas são os nós do arquivo (inclusive os que
// não são ossos, como o "Armature"), assim a pose global já sai no espaço do modelo.
struct Skeleton {
    struct Joint {
        std::string name;  // único no esqueleto (os clipes de outros arquivos acham a junta pelo nome)
        int parent = -1;   // índice da junta pai (sempre menor que o da filha); -1 = raiz
        JointTransform rest;
    };
    std::vector<Joint> joints;
    std::vector<glm::mat4> restGlobal;  // pose de repouso no espaço do modelo (Finalize)

    // Confere a ordem (pai antes da filha) e calcula restGlobal. false se a hierarquia estiver errada.
    bool Finalize();
    int Find(std::string_view name) const;  // -1 se não existir

    // Matrizes globais (espaço do modelo) a partir das poses locais de todas as juntas.
    void ComputeGlobals(std::span<const JointTransform> locals, std::span<glm::mat4> out) const;
    std::vector<JointTransform> RestPose() const;
};

// Pele de uma malha: quais juntas do esqueleto movem os vértices e a matriz inversa de "bind" de cada uma.
struct Skin {
    std::shared_ptr<const Skeleton> skeleton;
    std::vector<int> joints;               // índice no esqueleto da junta i da pele
    std::vector<glm::mat4> inverseBind;    // mesmo tamanho de `joints`

    // Matriz de cada junta da pele (global * inversa do bind), a partir das globais de todo o esqueleto.
    void JointMatrices(std::span<const glm::mat4> globals, std::vector<glm::mat4>& out) const;
};

// Influência das juntas num vértice (até 4). `joints` guarda os índices da pele como float (o shader lê
// assim); pesos somando 1.
struct SkinWeights {
    glm::vec4 joints{0.0f};
    glm::vec4 weights{1.0f, 0.0f, 0.0f, 0.0f};
};

// O que o shader faz com cada vértice, na CPU.
glm::vec3 SkinPosition(const glm::vec3& position, const SkinWeights& weights, std::span<const glm::mat4> jointMatrices);

} // namespace motor
