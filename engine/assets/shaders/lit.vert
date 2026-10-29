#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

#include "frame.glsl"

// SINGLE_MODEL: uma malha por desenho, com a matriz em uniform (contornos do editor, unlit.frag).
#ifdef SINGLE_MODEL
uniform mat4 uModel;
#else
#include "object.glsl"
#endif

out VS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
#ifdef TERRAIN
    flat vec2 corner;  // canto do bloco, em metros do terreno (os vértices partem dele; uv são os metros)
#endif
} vs;

void main() {
#ifdef SINGLE_MODEL
    mat4 model = uModel;
    mat3 normalMatrix = transpose(inverse(mat3(uModel)));
#else
    Object object = CurrentObject();
    mat4 model = object.model;
    mat3 normalMatrix = mat3(object.normal);
#endif
    vec4 world = model * vec4(aPosition, 1.0);
    vs.worldPos = world.xyz;
    vs.normal = normalMatrix * aNormal;
    vs.uv = aUV;
#ifdef TERRAIN
    vs.corner = aUV - aPosition.xz;
#endif
    gl_Position = uProjection * uView * world;
}
