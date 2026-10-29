#version 450 core

// Água (Fase 28): grade plana (motor::CreateWaterGrid) deslocada por 4 ondas de Gerstner. A conta é a mesma do
// WaterSurface::WaveOffset (Water.cpp): muda uma, muda a outra.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

#include "frame.glsl"

uniform mat4 uModel;  // relativa à câmera, como todo objeto (Fase 28)
uniform vec4 uWave;   // x = altura, y = comprimento, z = velocidade, w = direção (rad)
uniform float uTime;

out VS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} vs;

const float kAngles[4] = float[](0.0, 0.55, -0.85, 1.6);
const float kLengths[4] = float[](1.0, 0.61, 0.38, 0.23);
const float kHeights[4] = float[](0.52, 0.26, 0.14, 0.08);
const float kSteepness = 0.6;
const float kGravity = 9.81;
const float kTwoPi = 6.28318530718;

void main() {
    // As ondas correm no espaço da água (posição em relação à entidade, já girada e escalada): não dependem da câmera.
    vec2 p = (mat3(uModel) * aPosition).xz;
    vec3 offset = vec3(0.0);
    vec3 normal = vec3(0.0, 1.0, 0.0);
    if (uWave.x > 0.0 && uWave.y > 0.0) {
        for (int i = 0; i < 4; ++i) {
            vec2 dir = vec2(cos(uWave.w + kAngles[i]), sin(uWave.w + kAngles[i]));
            float k = kTwoPi / (uWave.y * kLengths[i]);
            float a = uWave.x * kHeights[i];
            float w = sqrt(kGravity * k) * uWave.z;
            float f = k * dot(dir, p) - w * uTime;
            float qa = min(kSteepness / (4.0 * k), a);
            float c = cos(f), s = sin(f);
            offset += vec3(qa * dir.x * c, a * s, qa * dir.y * c);
            // Normal da superfície de Gerstner (somas das derivadas de cada onda).
            normal.xz -= dir * (k * a * c);
            normal.y -= k * qa * s;
        }
    }
    vec4 world = uModel * vec4(aPosition, 1.0);
    world.xyz += offset;
    vs.worldPos = world.xyz;
    vs.normal = normal;
    vs.uv = aUV;
    gl_Position = uProjection * uView * world;
}
