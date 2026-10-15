#version 450 core

// Cor sólida (sRGB), sem iluminação. Usado para linhas e overlays do editor.

uniform vec4 uColor;

out vec4 oColor;

void main() {
    oColor = uColor;
}
