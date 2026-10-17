#version 450 core

// Igual ao lit.vert, com a pele: cada vértice é movido por até 4 juntas (Renderer::SubmitSkinned).
// Usado com o lit.frag.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aJoints;   // índices na pele (float)
layout(location = 4) in vec4 aWeights;  // somam 1

#include "frame.glsl"

// Mesmo tamanho que Renderer::kMaxJoints.
const int kMaxJoints = 128;
layout(std140, binding = 2) uniform Joints {
    mat4 uJoints[kMaxJoints];
};

uniform mat4 uModel;
uniform mat3 uNormalMatrix;

out VS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} vs;

void main() {
    mat4 skin = mat4(0.0);
    for (int i = 0; i < 4; ++i) {
        int joint = clamp(int(aJoints[i] + 0.5), 0, kMaxJoints - 1);
        skin += aWeights[i] * uJoints[joint];
    }
    vec4 local = skin * vec4(aPosition, 1.0);
    vec4 world = uModel * local;
    vs.worldPos = world.xyz;
    vs.normal = uNormalMatrix * (mat3(skin) * aNormal);
    vs.uv = aUV;
    gl_Position = uProjection * uView * world;
}
