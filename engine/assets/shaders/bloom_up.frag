#version 450 core

// Bloom, subida: filtro tenda 3x3 do nível menor, somado (blend aditivo) ao nível maior.

in vec2 vUv;

layout(binding = 0) uniform sampler2D uSource;
uniform vec2 uTexel;  // 1 / tamanho da fonte

out vec4 oColor;

vec3 Tap(vec2 offset) {
    return texture(uSource, vUv + offset * uTexel).rgb;
}

void main() {
    vec3 sum = Tap(vec2(0.0)) * 4.0;
    sum += (Tap(vec2(-1.0, 0.0)) + Tap(vec2(1.0, 0.0)) + Tap(vec2(0.0, -1.0)) + Tap(vec2(0.0, 1.0))) * 2.0;
    sum += Tap(vec2(-1.0, -1.0)) + Tap(vec2(1.0, -1.0)) + Tap(vec2(-1.0, 1.0)) + Tap(vec2(1.0, 1.0));
    oColor = vec4(sum / 16.0, 1.0);
}
