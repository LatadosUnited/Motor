#version 450 core

// Fundo da cena: o cubemap do céu na direção de cada pixel (desenhado depois dos objetos, só onde não há nada).

in vec2 vUv;

#include "frame.glsl"
#include "pbr.glsl"

layout(binding = 0) uniform samplerCube uEnvironment;

out vec4 oColor;

void main() {
    vec4 world = uInvViewProjection * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(world.xyz / world.w - uCameraPosition.xyz);
    oColor = vec4(textureLod(uEnvironment, RotateY(dir, -uSky.z), 0.0).rgb * uSky.x, 1.0);
}
