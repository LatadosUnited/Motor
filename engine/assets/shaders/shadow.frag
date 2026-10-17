#version 450 core

// Direcional: só a profundidade do rasterizador. POINT (pontuais/spots): a distância até a luz dividida pelo
// alcance, igual em todas as faces do cubo (o lit.frag compara com a mesma conta).

in vec3 vWorldPos;

#ifdef POINT
uniform vec4 uLightPositionRange;  // xyz = posição, w = alcance
#endif

void main() {
#ifdef POINT
    gl_FragDepth = clamp(length(vWorldPos - uLightPositionRange.xyz) / uLightPositionRange.w, 0.0, 1.0);
#endif
}
