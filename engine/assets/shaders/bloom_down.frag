#version 450 core

// Bloom, descida: reduz a imagem pela metade com o filtro de 13 amostras (Jimenez, CoD: Advanced Warfare).
// Na primeira passada (uPrefilter = 1), da imagem inteira, um filtro mais barato e só a luz acima do limiar.

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

// Peso de Karis: um pixel muito brilhante sozinho não domina a média (sem "vagalumes" piscando).
float KarisWeight(vec3 c) { return 1.0 / (1.0 + max(c.r, max(c.g, c.b))); }

void main() {
    if (uPrefilter == 1) {
        // Primeira passada, da imagem inteira: 4 leituras bilineares (os mesmos 4x4 pixels de origem) em vez das 13,
        // que pesavam na GPU integrada por ler a cena em resolução cheia.
        vec3 p = Tap(vec2(-1.0, 1.0)), q = Tap(vec2(1.0, 1.0)), r = Tap(vec2(-1.0, -1.0)), s = Tap(vec2(1.0, -1.0));
        float wp = KarisWeight(p), wq = KarisWeight(q), wr = KarisWeight(r), ws = KarisWeight(s);
        oColor = vec4(Threshold((p * wp + q * wq + r * wr + s * ws) / (wp + wq + wr + ws)), 1.0);
        return;
    }
    vec3 a = Tap(vec2(-2.0, 2.0)), b = Tap(vec2(0.0, 2.0)), c = Tap(vec2(2.0, 2.0));
    vec3 d = Tap(vec2(-2.0, 0.0)), e = Tap(vec2(0.0, 0.0)), f = Tap(vec2(2.0, 0.0));
    vec3 g = Tap(vec2(-2.0, -2.0)), h = Tap(vec2(0.0, -2.0)), i = Tap(vec2(2.0, -2.0));
    vec3 j = Tap(vec2(-1.0, 1.0)), k = Tap(vec2(1.0, 1.0)), l = Tap(vec2(-1.0, -1.0)), m = Tap(vec2(1.0, -1.0));
    vec3 color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    oColor = vec4(color, 1.0);
}
