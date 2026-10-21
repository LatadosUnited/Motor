#pragma once

// Contas do PBR e da luz do céu na CPU, sem OpenGL (testadas no ctest, test_render). As funções com "(GLSL)"
// têm uma cópia igual nos shaders (pbr.glsl, sky_cube.frag, prefilter.frag): mudou uma, mude a outra.

#include "motor/renderer/Environment.h"
#include "motor/renderer/Material.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace motor {

inline constexpr float kPi = 3.14159265358979f;

// --- BRDF (Cook-Torrance com GGX) --------------------------------------------------------------------------

// Distribuição das micro-facetas (GGX/Trowbridge-Reitz), com alfa = rugosidade². (GLSL)
float DistributionGgx(float NdotH, float roughness);
// Visibilidade de Smith com correlação (já dividida por 4·NdotV·NdotL). (GLSL)
float VisibilitySmithGgx(float NdotV, float NdotL, float roughness);
glm::vec3 FresnelSchlick(float cosTheta, const glm::vec3& f0);  // (GLSL)
// Reflexo de quem olha de frente: não metal = specular * 8% (0.5 => 4%), metal = a própria cor. (GLSL)
glm::vec3 BaseReflectance(const glm::vec3& albedo, float metallic, float specular);

// Sequência de Hammersley (amostras bem espalhadas no quadrado [0,1)²). (GLSL)
glm::vec2 Hammersley(uint32_t i, uint32_t count);
// Meio-vetor H amostrado conforme a GGX em volta de N. (GLSL)
glm::vec3 ImportanceSampleGgx(const glm::vec2& xi, const glm::vec3& N, float roughness);

// Parte do reflexo do céu que não depende do céu (split sum de Karis): reflexo = céu filtrado * (F0 * x + y).
glm::vec2 IntegrateBrdf(float NdotV, float roughness, uint32_t samples = 256);
// Tabela de IntegrateBrdf, size x size: x = NdotV, y = rugosidade (centro dos texels). Índice y * size + x.
std::vector<glm::vec2> BuildBrdfLut(int size, uint32_t samples = 128);

// --- Luz do céu ---------------------------------------------------------------------------------------------

// Harmônicos esféricos de ordem 2 (9 coeficientes RGB): luz difusa do céu barata de avaliar no shader.
struct Sh9 {
    std::array<glm::vec3, 9> c{};
};
// Projeta a radiância (função da direção) numa grade latitude-longitude de 2*resolution x resolution.
Sh9 ProjectSh9(const std::function<glm::vec3(const glm::vec3& direction)>& radiance, int resolution = 32);
// Coeficientes de radiância -> coeficientes de irradiância (convolução com o cosseno, Ramamoorthi-Hanrahan).
Sh9 ConvolveIrradiance(const Sh9& radiance);
// Soma dos coeficientes na direção n (com os de ConvolveIrradiance, dá a irradiância E). (GLSL)
glm::vec3 EvaluateSh9(const Sh9& sh, const glm::vec3& n);

// Céu de cor (modo SkyMode::Color): degradê do zênite ao chão a partir da cor (linear) do céu. (GLSL)
glm::vec3 GradientSky(const glm::vec3& direction, const glm::vec3& skyLinear);
// Direção -> coordenadas da imagem equirretangular (u: longitude, v: 0 no topo). (GLSL)
glm::vec2 EquirectUv(const glm::vec3& direction);
glm::vec3 SampleEquirect(const HdrImage& image, const glm::vec3& direction);  // bilinear
glm::vec3 RotateY(const glm::vec3& v, float radians);  // (GLSL)

glm::vec3 SrgbToLinear(const glm::vec3& srgb);  // gama 2.2, como o resto do renderer

// Radiância do céu na direção (mundo): o que o fundo mostra e o que ilumina a cena, sem a intensidade.
// Modo textura sem imagem cai na cor.
glm::vec3 SkyRadiance(const Environment& environment, const glm::vec3& direction);

// --- Material no lit.frag -----------------------------------------------------------------------------------

// O que um material manda para o lit.frag: os uniforms já convertidos (emissivo linear, recorte limitado) e as
// texturas (nula = a branca do renderer). O renderer compara com o do desenho anterior e só reenvia o que mudou.
struct MaterialParams {
    glm::vec4 alphaMode{0.0f};  // x = BlendMode, y = alphaCutoff
    glm::vec4 color{1.0f};
    glm::vec4 pbr{0.0f};        // metallic, roughness, specular, força do mapa de normal (0 = sem mapa)
    glm::vec4 emissive{0.0f};   // rgb = emissivo linear * intensidade, a = força da oclusão (0 = sem mapa)
    const Texture2D* albedo = nullptr;
    const Texture2D* normalMap = nullptr;
    const Texture2D* metallicRoughnessMap = nullptr;
    const Texture2D* occlusionMap = nullptr;
    const Texture2D* emissiveMap = nullptr;

    bool operator==(const MaterialParams&) const = default;
};

MaterialParams MaterialParamsOf(const Material& material);

} // namespace motor
