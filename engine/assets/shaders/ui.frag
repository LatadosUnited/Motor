#version 450 core

// UI do jogo (Renderer2D). Imagens: textura x cor. Texto: o alfa do atlas é a distância até a borda do glifo
// (SDF, 0.5 = borda); o recorte é suavizado pela largura de um pixel na tela, nítido em qualquer tamanho.

in vec2 vUv;
in vec4 vColor;
in float vSdf;

uniform sampler2D uTexture;
uniform int uSrgbTexture;  // texturas sRGB chegam lineares na amostragem: voltam para sRGB, como as cores

out vec4 oColor;

void main() {
    vec4 texel = texture(uTexture, vUv);
    if (vSdf > 0.5) {
        float distance = texel.a;
        float width = max(fwidth(distance) * 0.7, 1e-4);
        float alpha = smoothstep(0.5 - width, 0.5 + width, distance);
        oColor = vec4(vColor.rgb, vColor.a * alpha);
    } else {
        if (uSrgbTexture != 0) texel.rgb = pow(texel.rgb, vec3(1.0 / 2.2));
        oColor = texel * vColor;
    }
}
