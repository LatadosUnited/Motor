#version 450 core

// Reflexo pré-filtrado: uma face de um mip do cubemap, convoluída com a GGX da rugosidade desse mip
// (amostragem por importância, lendo mips mais borrados da fonte conforme a densidade das amostras).

in vec2 vUv;

#include "pbr.glsl"

layout(binding = 0) uniform samplerCube uSource;
uniform int uFace;
uniform float uRoughness;
uniform float uSourceSize;  // lado da face do mip 0 da fonte

out vec4 oColor;

const uint kSamples = 96u;

void main() {
    vec3 N = CubeDirection(uFace, vUv);
    vec3 V = N;
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    float texelSolidAngle = 4.0 * PI / (6.0 * uSourceSize * uSourceSize);
    for (uint i = 0u; i < kSamples; ++i) {
        vec3 H = ImportanceSampleGgx(Hammersley(i, kSamples), N, uRoughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = dot(N, L);
        if (NdotL <= 0.0) continue;
        float NdotH = max(dot(N, H), 0.0);
        float pdf = DistributionGgx(NdotH, uRoughness) * 0.25 + 1e-4;  // NdotH/(4·VdotH) = 1/4 com V = N
        float sampleSolidAngle = 1.0 / (float(kSamples) * pdf);
        float mip = 0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0;
        sum += textureLod(uSource, L, max(mip, 0.0)).rgb * NdotL;
        weight += NdotL;
    }
    oColor = vec4(sum / max(weight, 1e-4), 1.0);
}
