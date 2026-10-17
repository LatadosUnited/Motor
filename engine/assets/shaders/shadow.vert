#version 450 core

// Profundidade vista da luz (mapas de sombra). SKINNED: com a pele, como o skinned.vert.

layout(location = 0) in vec3 aPosition;
#ifdef SKINNED
layout(location = 3) in vec4 aJoints;
layout(location = 4) in vec4 aWeights;

const int kMaxJoints = 128;  // Renderer::kMaxJoints
layout(std140, binding = 2) uniform Joints {
    mat4 uJoints[kMaxJoints];
};
#endif

uniform mat4 uModel;
uniform mat4 uLightViewProjection;

out vec3 vWorldPos;

void main() {
    vec4 local = vec4(aPosition, 1.0);
#ifdef SKINNED
    mat4 skin = mat4(0.0);
    for (int i = 0; i < 4; ++i) {
        int joint = clamp(int(aJoints[i] + 0.5), 0, kMaxJoints - 1);
        skin += aWeights[i] * uJoints[joint];
    }
    local = skin * local;
#endif
    vec4 world = uModel * local;
    vWorldPos = world.xyz;
    gl_Position = uLightViewProjection * world;
}
