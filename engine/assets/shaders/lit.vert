#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(std140, binding = 0) uniform Frame {
    mat4 uView;
    mat4 uProjection;
    vec4 uCameraPosition;
    vec4 uAmbient;
    vec4 uFog;  // rgb = cor do céu (linear), a = densidade
};

uniform mat4 uModel;
uniform mat3 uNormalMatrix;

out VS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} vs;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vs.worldPos = world.xyz;
    vs.normal = uNormalMatrix * aNormal;
    vs.uv = aUV;
    gl_Position = uProjection * uView * world;
}
