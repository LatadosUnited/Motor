#pragma once

// Contas das sombras e do recorte (culling) na CPU, sem OpenGL: testadas no ctest (test_render).

#include "motor/renderer/Camera.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <vector>

namespace motor {

struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

// Caixa (alinhada aos eixos) que contém a caixa `box` transformada por `m`.
Aabb TransformAabb(const Aabb& box, const glm::mat4& m);

// Os seis planos de uma matriz de projeção*vista (normais para dentro).
struct Frustum {
    std::array<glm::vec4, 6> planes{};  // esquerda, direita, baixo, cima, perto, longe

    static Frustum FromMatrix(const glm::mat4& viewProjection);
    // false só se a caixa está toda fora de algum plano (pode dar falso positivo nos cantos, nunca falso negativo).
    bool Intersects(const Aabb& box) const;
    bool IntersectsSphere(const glm::vec3& center, float radius) const;
};

// Onde termina cada cascata (distância a partir da câmera), da mais perto à mais longe; a última é `farPlane`.
// Esquema "prático": mistura da divisão logarítmica (lambda = 1) com a uniforme (lambda = 0).
std::vector<float> CascadeSplits(float nearPlane, float farPlane, int count, float lambda = 0.75f);

// Os 8 cantos do pedaço do frustum da câmera entre as distâncias `nearDist` e `farDist` (no mundo).
std::array<glm::vec3, 8> FrustumSliceCorners(const Camera& camera, float nearDist, float farDist);

struct Cascade {
    glm::mat4 viewProjection{1.0f};  // mundo -> espaço da sombra (clip ortográfico da luz)
    float texelSize = 0.0f;          // tamanho de um texel no mundo (para o viés na normal)
    glm::vec3 center{0.0f};
    float radius = 0.0f;
};

// Cascata que cobre o pedaço [nearDist, farDist] do frustum da câmera, vista pela luz com direção `lightDir`.
// Usa uma esfera em volta do pedaço (o tamanho não muda quando a câmera gira) com o centro preso à grade de
// texels do mapa (a sombra não "treme" quando a câmera anda). `casterDepth`: quanto antes da esfera, na
// direção da luz, objetos ainda projetam sombra dentro dela.
Cascade ComputeCascade(const Camera& camera, float nearDist, float farDist, const glm::vec3& lightDir,
                       int resolution, float casterDepth = 50.0f);

// Sombra de pontual/spot: as seis faces do cubo (+X, -X, +Y, -Y, +Z, -Z, a ordem do OpenGL) vistas da luz.
glm::mat4 CubeFaceView(const glm::vec3& position, int face);
glm::mat4 CubeFaceProjection(float nearPlane, float farPlane);  // 90°, quadrada

} // namespace motor
