#version 450 core

// Triângulo que cobre a tela toda (sem buffer de vértices: só gl_VertexID). Profundidade 1 = no fundo.

out vec2 vUv;

void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 1.0, 1.0);
}
