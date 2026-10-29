#pragma once

// Água (Fase 28, Tarefa 13): superfície com ondas de Gerstner (4 ondas em volta da direção principal, no vertex
// shader), reflexo do céu (o mapa pré-filtrado, com Fresnel), refração (a imagem da cena por baixo, distorcida pelas
// ondas) e cor pela profundidade (a água mais grossa entre a superfície e o fundo puxa da cor rasa para a funda), com
// espuma na beira. O Renderer desenha depois dos opacos e do céu (Renderer::SubmitWater). WaveOffset faz na CPU a mesma
// conta do water.vert: quem precisar da altura da água num ponto (boias, testes) usa ela.

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <memory>

namespace motor {

class Mesh;

struct WaterSurface {
    glm::vec3 shallowColor{0.16f, 0.55f, 0.56f};  // sRGB: a água rasa (a cor espalhada por ela, sobre o fundo)
    glm::vec3 deepColor{0.02f, 0.13f, 0.22f};     // sRGB: a água funda
    float depthFade = 4.0f;    // metros de água (no raio de visão) até o fundo quase sumir
    float waveHeight = 0.25f;  // amplitude das ondas somadas, em metros (0 = parada)
    float waveLength = 12.0f;  // metros da onda maior
    float waveSpeed = 1.0f;    // 1 = a velocidade natural de uma onda desse tamanho
    float waveDirection = 0.0f;  // graus, em volta de Y (0 = para +X)
    float reflection = 1.0f;   // força do reflexo do céu (0 a 1)
    float refraction = 0.03f;  // distorção do fundo pelas ondas (fração da tela)
    float foam = 0.6f;         // metros de espuma na beira (0 = sem)

    // Deslocamento da superfície no ponto (x, z) do espaço da água, no tempo `seconds`: x e z andam com a onda
    // (Gerstner), y é a altura. Igual ao water.vert.
    glm::vec3 WaveOffset(const glm::vec2& position, float seconds) const;
};

// Grade plana de `size` metros centrada na origem (plano XZ, normal +Y), com células de até `cellSize` metros (no
// máximo 256 por lado). O UV é a posição em metros.
std::shared_ptr<Mesh> CreateWaterGrid(const glm::vec2& size, float cellSize = 1.0f);

} // namespace motor
