#version 450 core

// UI do jogo (Renderer2D): posições em pixels, origem no canto superior esquerdo, y para baixo.

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aSdf;

uniform vec2 uScreen;

out vec2 vUv;
out vec4 vColor;
out float vSdf;

void main() {
    vUv = aUv;
    vColor = aColor;
    vSdf = aSdf;
    gl_Position = vec4(aPosition.x / uScreen.x * 2.0 - 1.0, 1.0 - aPosition.y / uScreen.y * 2.0, 0.0, 1.0);
}
