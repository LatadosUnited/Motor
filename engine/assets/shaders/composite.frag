#version 450 core

// HDR -> tela: exposição, bloom, tone mapping, gama 2.2 e um pouco de ruído (sem faixas no degradê do céu).
// Alfa = luminância, que o FXAA usa.

in vec2 vUv;

layout(binding = 0) uniform sampler2D uHdr;
layout(binding = 1) uniform sampler2D uBloom;
uniform float uExposure;
uniform float uBloomIntensity;  // 0 = sem bloom
uniform int uToneMapper;        // motor::ToneMapper: 0 = ACES, 1 = Reinhard, 2 = nenhum

out vec4 oColor;

// ACES ajustado por Stephen Hill (RRT + ODT), em sRGB linear.
vec3 Aces(vec3 color) {
    const mat3 inputMatrix = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
    const mat3 outputMatrix = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
    vec3 v = inputMatrix * color;
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return clamp(outputMatrix * (a / b), 0.0, 1.0);
}

void main() {
    vec3 color = texture(uHdr, vUv).rgb;
    if (uBloomIntensity > 0.0) color += texture(uBloom, vUv).rgb * uBloomIntensity;
    color *= uExposure;
    if (uToneMapper == 0) {
        color = Aces(color);
    } else if (uToneMapper == 1) {
        color = color / (1.0 + color);
    }
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    float noise = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    color += (noise - 0.5) / 255.0;
    oColor = vec4(color, dot(color, vec3(0.299, 0.587, 0.114)));
}
