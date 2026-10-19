#version 450 core

// Direcional: só a profundidade do rasterizador. POINT (pontuais/spots): a distância até a luz dividida pelo
// alcance, igual em todas as faces do cubo (o lit.frag compara com a mesma conta).

in vec3 vWorldPos;
in vec2 vUV;

// Material de recorte: a sombra também é recortada (o mesmo alfa do lit.frag). uAlphaCutoff < 0: opaco.
layout(binding = 0) uniform sampler2D uAlbedo;
uniform float uAlphaCutoff;
uniform float uColorAlpha;

#ifdef POINT
uniform vec4 uLightPositionRange;  // xyz = posição, w = alcance
#endif

void main() {
    if (uAlphaCutoff >= 0.0 && texture(uAlbedo, vUV).a * uColorAlpha < uAlphaCutoff) discard;
#ifdef POINT
    gl_FragDepth = clamp(length(vWorldPos - uLightPositionRange.xyz) / uLightPositionRange.w, 0.0, 1.0);
#endif
}
