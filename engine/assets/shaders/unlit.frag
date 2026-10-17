#version 450 core

// Cor sólida (sRGB), sem iluminação. Usado para linhas e overlays do editor, desenhados depois do
// pós-processamento: a profundidade da cena vem da textura (só aparece o que não está atrás de algo).

#include "frame.glsl"

layout(binding = 0) uniform sampler2D uSceneDepth;
uniform vec4 uColor;

out vec4 oColor;

float ViewDistance(float depth) {
    return uProjection[3][2] / ((depth * 2.0 - 1.0) + uProjection[2][2]);
}

void main() {
    float scene = ViewDistance(texelFetch(uSceneDepth, ivec2(gl_FragCoord.xy), 0).r);
    float self = ViewDistance(gl_FragCoord.z);
    if (self > scene * 1.003 + 0.003) discard;
    oColor = uColor;
}
