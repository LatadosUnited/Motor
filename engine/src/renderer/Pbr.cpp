#include "motor/renderer/Pbr.h"

#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace motor {

namespace {
float Alpha(float roughness) {
    const float r = std::clamp(roughness, 0.02f, 1.0f);
    return r * r;
}

// Funções da base real dos harmônicos esféricos até a banda 2.
std::array<float, 9> ShBasis(const glm::vec3& n) {
    return {0.282095f,
            0.488603f * n.y,
            0.488603f * n.z,
            0.488603f * n.x,
            1.092548f * n.x * n.y,
            1.092548f * n.y * n.z,
            0.315392f * (3.0f * n.z * n.z - 1.0f),
            1.092548f * n.x * n.z,
            0.546274f * (n.x * n.x - n.y * n.y)};
}
} // namespace

float DistributionGgx(float NdotH, float roughness) {
    const float a2 = Alpha(roughness) * Alpha(roughness);
    const float d = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
}

float VisibilitySmithGgx(float NdotV, float NdotL, float roughness) {
    const float a2 = Alpha(roughness) * Alpha(roughness);
    const float ggxV = NdotL * std::sqrt(NdotV * NdotV * (1.0f - a2) + a2);
    const float ggxL = NdotV * std::sqrt(NdotL * NdotL * (1.0f - a2) + a2);
    const float sum = ggxV + ggxL;
    return sum > 0.0f ? 0.5f / sum : 0.0f;
}

glm::vec3 FresnelSchlick(float cosTheta, const glm::vec3& f0) {
    const float f = std::pow(1.0f - std::clamp(cosTheta, 0.0f, 1.0f), 5.0f);
    return f0 + (glm::vec3(1.0f) - f0) * f;
}

glm::vec3 BaseReflectance(const glm::vec3& albedo, float metallic, float specular) {
    return glm::mix(glm::vec3(0.08f * std::max(specular, 0.0f)), albedo, std::clamp(metallic, 0.0f, 1.0f));
}

glm::vec2 Hammersley(uint32_t i, uint32_t count) {
    uint32_t bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return {static_cast<float>(i) / static_cast<float>(std::max(count, 1u)), static_cast<float>(bits) * 2.3283064365386963e-10f};
}

glm::vec3 ImportanceSampleGgx(const glm::vec2& xi, const glm::vec3& N, float roughness) {
    const float a = Alpha(roughness);
    const float phi = 2.0f * kPi * xi.x;
    const float cosTheta = std::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const glm::vec3 h{std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta};
    const glm::vec3 up = std::abs(N.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 t = glm::normalize(glm::cross(up, N));
    const glm::vec3 b = glm::cross(N, t);
    return glm::normalize(t * h.x + b * h.y + N * h.z);
}

glm::vec2 IntegrateBrdf(float NdotV, float roughness, uint32_t samples) {
    NdotV = std::clamp(NdotV, 1e-4f, 1.0f);
    const glm::vec3 V{std::sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV};
    const glm::vec3 N{0.0f, 0.0f, 1.0f};
    float a = 0.0f, b = 0.0f;
    for (uint32_t i = 0; i < samples; ++i) {
        const glm::vec3 H = ImportanceSampleGgx(Hammersley(i, samples), N, roughness);
        const float VdotH = glm::dot(V, H);
        const glm::vec3 L = 2.0f * VdotH * H - V;
        const float NdotL = L.z;
        const float NdotH = H.z;
        if (NdotL <= 0.0f || VdotH <= 0.0f) continue;
        // pdf(L) = D·NdotH / (4·VdotH): o D some e sobra V·4·NdotL·VdotH/NdotH.
        const float vis = VisibilitySmithGgx(NdotV, NdotL, roughness) * 4.0f * NdotL * VdotH / NdotH;
        const float fc = std::pow(1.0f - VdotH, 5.0f);
        a += (1.0f - fc) * vis;
        b += fc * vis;
    }
    const float n = static_cast<float>(std::max(samples, 1u));
    return {a / n, b / n};
}

std::vector<glm::vec2> BuildBrdfLut(int size, uint32_t samples) {
    size = std::max(size, 1);
    std::vector<glm::vec2> lut(static_cast<size_t>(size * size));
    for (int y = 0; y < size; ++y) {
        const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
        for (int x = 0; x < size; ++x) {
            const float NdotV = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            lut[static_cast<size_t>(y * size + x)] = IntegrateBrdf(NdotV, roughness, samples);
        }
    }
    return lut;
}

Sh9 ProjectSh9(const std::function<glm::vec3(const glm::vec3&)>& radiance, int resolution) {
    const int h = std::max(resolution, 2);
    const int w = 2 * h;
    const float dTheta = kPi / static_cast<float>(h);
    const float dPhi = 2.0f * kPi / static_cast<float>(w);
    Sh9 sh;
    for (int j = 0; j < h; ++j) {
        const float theta = (static_cast<float>(j) + 0.5f) * dTheta;
        const float sinTheta = std::sin(theta);
        const float dOmega = sinTheta * dTheta * dPhi;
        for (int i = 0; i < w; ++i) {
            const float phi = (static_cast<float>(i) + 0.5f) * dPhi;
            const glm::vec3 dir{sinTheta * std::cos(phi), std::cos(theta), sinTheta * std::sin(phi)};
            const glm::vec3 l = radiance(dir) * dOmega;
            const auto y = ShBasis(dir);
            for (size_t k = 0; k < 9; ++k) sh.c[k] += l * y[k];
        }
    }
    return sh;
}

Sh9 ConvolveIrradiance(const Sh9& radiance) {
    constexpr float kBand[3] = {kPi, 2.0f * kPi / 3.0f, kPi / 4.0f};
    Sh9 out;
    for (size_t k = 0; k < 9; ++k) out.c[k] = radiance.c[k] * kBand[k == 0 ? 0 : k < 4 ? 1 : 2];
    return out;
}

glm::vec3 EvaluateSh9(const Sh9& sh, const glm::vec3& n) {
    const auto y = ShBasis(n);
    glm::vec3 sum(0.0f);
    for (size_t k = 0; k < 9; ++k) sum += sh.c[k] * y[k];
    return sum;
}

glm::vec3 GradientSky(const glm::vec3& direction, const glm::vec3& sky) {
    const float y = std::clamp(direction.y, -1.0f, 1.0f);
    if (y >= 0.0f) return glm::mix(sky, sky * 0.75f, std::pow(y, 0.6f));
    const float t = std::clamp(-y / 0.25f, 0.0f, 1.0f);
    return glm::mix(sky, sky * 0.3f, t * t * (3.0f - 2.0f * t));
}

glm::vec2 EquirectUv(const glm::vec3& d) {
    return {std::atan2(d.x, -d.z) / (2.0f * kPi) + 0.5f, std::acos(std::clamp(d.y, -1.0f, 1.0f)) / kPi};
}

glm::vec3 SampleEquirect(const HdrImage& image, const glm::vec3& direction) {
    if (image.width <= 0 || image.height <= 0 || image.rgb.size() < static_cast<size_t>(image.width * image.height * 3)) {
        return glm::vec3(0.0f);
    }
    const glm::vec2 uv = EquirectUv(glm::normalize(direction));
    const float fx = uv.x * static_cast<float>(image.width) - 0.5f;
    const float fy = uv.y * static_cast<float>(image.height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    auto at = [&](int x, int y) {
        x = ((x % image.width) + image.width) % image.width;  // longitude dá a volta
        y = std::clamp(y, 0, image.height - 1);
        return image.Pixel(x, y);
    };
    return glm::mix(glm::mix(at(x0, y0), at(x0 + 1, y0), tx), glm::mix(at(x0, y0 + 1), at(x0 + 1, y0 + 1), tx), ty);
}

glm::vec3 RotateY(const glm::vec3& v, float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    return {c * v.x + s * v.z, v.y, -s * v.x + c * v.z};
}

glm::vec3 SrgbToLinear(const glm::vec3& srgb) {
    return glm::pow(glm::max(srgb, glm::vec3(0.0f)), glm::vec3(2.2f));
}

glm::vec3 SkyRadiance(const Environment& env, const glm::vec3& direction) {
    if (env.skyMode == SkyMode::Texture && env.skyTexture) {
        return SampleEquirect(*env.skyTexture, RotateY(direction, -glm::radians(env.skyRotation)));
    }
    return GradientSky(direction, SrgbToLinear(env.skyColor));
}

MaterialParams MaterialParamsOf(const Material& m) {
    MaterialParams p;
    // z: os mapas que o material tem (kMapAlbedo...): o lit.frag não lê os que faltam. Ler a textura branca de 1x1
    // custava ~4 ms por quadro na referência (HD 530), com os 4 mapas de cada pixel.
    const int maps = (m.albedo ? kMapAlbedo : 0) | (m.metallicRoughnessMap ? kMapMetallicRoughness : 0) |
                     (m.occlusionMap ? kMapOcclusion : 0) | (m.emissiveMap ? kMapEmissive : 0);
    p.alphaMode = glm::vec4(static_cast<float>(m.blend), std::clamp(m.alphaCutoff, 0.0f, 1.0f), static_cast<float>(maps), 0.0f);
    p.color = m.color;
    p.pbr = glm::vec4(m.metallic, m.roughness, m.specular, m.normalMap ? m.normalStrength : 0.0f);
    // Sem luz própria (o comum), sem as 3 potências: o renderer chama isto a cada desenho.
    const bool glows = m.emissiveIntensity > 0.0f && m.emissive != glm::vec3(0.0f);
    p.emissive = glm::vec4(glows ? SrgbToLinear(m.emissive) * m.emissiveIntensity : glm::vec3(0.0f),
                           m.occlusionMap ? std::clamp(m.occlusionStrength, 0.0f, 1.0f) : 0.0f);
    p.albedo = m.albedo.get();
    p.normalMap = m.normalMap.get();
    p.metallicRoughnessMap = m.metallicRoughnessMap.get();
    p.occlusionMap = m.occlusionMap.get();
    p.emissiveMap = m.emissiveMap.get();
    return p;
}

} // namespace motor
