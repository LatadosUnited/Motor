#version 450 core

// Bloom, descida: reduz a imagem pela metade com o filtro de 13 amostras (Jimenez, CoD: Advanced Warfare).
// Na primeira passada (uPrefilter = 1) só passa a luz acima do limiar, com joelho suave.

in vec2 vUv;

layout(binding = 0) uniform sampler2D uSource;
uniform vec2 uTexel;       // 1 / tamanho da fonte
uniform int uPrefilter;
uniform float uThreshold;

out vec4 oColor;

vec3 Threshold(vec3 c) {
    c = min(c, vec3(256.0));  // pixels muito brilhantes viram "vagalumes" piscando
    float brightness = max(c.r, max(c.g, c.b));
    float knee = max(uThreshold * 0.5, 1e-4);
    float soft = clamp(brightness - uThreshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    return c * (max(soft, brightness - uThreshold) / max(brightness, 1e-4));
}

vec3 Tap(vec2 offset) {
    return texture(uSource, vUv + offset * uTexel).rgb;
}

void main() {
    vec3 a = Tap(vec2(-2.0, 2.0)), b = Tap(vec2(0.0, 2.0)), c = Tap(vec2(2.0, 2.0));
    vec3 d = Tap(vec2(-2.0, 0.0)), e = Tap(vec2(0.0, 0.0)), f = Tap(vec2(2.0, 0.0));
    vec3 g = Tap(vec2(-2.0, -2.0)), h = Tap(vec2(0.0, -2.0)), i = Tap(vec2(2.0, -2.0));
    vec3 j = Tap(vec2(-1.0, 1.0)), k = Tap(vec2(1.0, 1.0)), l = Tap(vec2(-1.0, -1.0)), m = Tap(vec2(1.0, -1.0));
    vec3 color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    if (uPrefilter == 1) color = Threshold(color);
    oColor = vec4(color, 1.0);
}
