#version 450 core

// Uma face do cubemap do céu: degradê a partir da cor (uMode = 0) ou a imagem equirretangular HDR (uMode = 1).

in vec2 vUv;

#include "pbr.glsl"

layout(binding = 0) uniform sampler2D uEquirect;
uniform int uFace;
uniform int uMode;
uniform vec3 uSkyColor;  // linear
uniform float uLod;      // mip da imagem para não serrilhar ao reduzir

out vec4 oColor;

void main() {
    vec3 dir = CubeDirection(uFace, vUv);
    vec3 color = uMode == 1 ? textureLod(uEquirect, EquirectUv(dir), uLod).rgb : GradientSky(dir, uSkyColor);
    oColor = vec4(min(color, vec3(65000.0)), 1.0);
}
