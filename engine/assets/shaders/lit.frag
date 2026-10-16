#version 450 core

// Blinn-Phong com N luzes (direcionais, pontuais e spots). Toda a iluminação é feita em espaço linear
// e convertida para sRGB (gama 2.2) na saída.

in VS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} fs;

layout(std140, binding = 0) uniform Frame {
    mat4 uView;
    mat4 uProjection;
    vec4 uCameraPosition;
    vec4 uAmbient;
    vec4 uFog;  // rgb = cor do céu (linear), a = densidade
};

// Mesmo tamanho que kMaxLights (motor/renderer/Light.h).
const int kMaxLights = 32;
const int kDirectional = 0;
const int kPoint = 1;
const int kSpot = 2;

struct LightData {
    vec4 positionRange;  // xyz = posição, w = alcance
    vec4 directionType;  // xyz = direção (normalizada), w = tipo
    vec4 color;          // rgb = cor * intensidade
    vec4 cone;           // x = cos(ângulo interno), y = cos(ângulo externo)
};

layout(std140, binding = 1) uniform Lights {
    ivec4 uLightCount;  // x = número de luzes
    LightData uLights[kMaxLights];
};

layout(binding = 0) uniform sampler2D uAlbedo;  // textura sRGB => amostrada já linear
uniform vec4 uColor;                            // sRGB
uniform float uSpecular;
uniform float uShininess;

out vec4 oColor;

// Queda relativa ao alcance (a intensidade não depende do tamanho do alcance): forte perto da lâmpada,
// chegando a zero exatamente no alcance, sem corte visível.
float RangeAttenuation(float dist, float range) {
    float ratio = dist / range;
    float window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    return window * window / (1.0 + 25.0 * ratio * ratio);
}

void main() {
    vec4 tint = vec4(pow(uColor.rgb, vec3(2.2)), uColor.a);
    vec4 albedo = texture(uAlbedo, fs.uv) * tint;

    vec3 N = normalize(fs.normal);
    vec3 V = normalize(uCameraPosition.xyz - fs.worldPos);

    vec3 diffuseSum = vec3(0.0);
    vec3 specularSum = vec3(0.0);
    int count = min(uLightCount.x, kMaxLights);
    for (int i = 0; i < count; ++i) {
        LightData light = uLights[i];
        int type = int(light.directionType.w + 0.5);

        vec3 L;
        float attenuation = 1.0;
        if (type == kDirectional) {
            L = -light.directionType.xyz;
        } else {
            vec3 toLight = light.positionRange.xyz - fs.worldPos;
            float dist = length(toLight);
            if (dist >= light.positionRange.w) continue;
            L = toLight / max(dist, 1e-4);
            attenuation = RangeAttenuation(dist, light.positionRange.w);
            if (type == kSpot) {
                float cosAngle = dot(-L, light.directionType.xyz);
                attenuation *= smoothstep(light.cone.y, light.cone.x, cosAngle);
            }
        }
        if (attenuation <= 0.0) continue;

        float diffuse = max(dot(N, L), 0.0);
        if (diffuse <= 0.0) continue;
        vec3 H = normalize(L + V);
        float specular = pow(max(dot(N, H), 0.0), uShininess) * uSpecular;

        vec3 radiance = light.color.rgb * attenuation;
        diffuseSum += diffuse * radiance;
        specularSum += specular * radiance;
    }

    vec3 color = albedo.rgb * (uAmbient.rgb + diffuseSum) + specularSum;

    // Neblina simples de distância, para dar profundidade.
    float dist = length(uCameraPosition.xyz - fs.worldPos);
    float fog = 1.0 - exp(-pow(dist * uFog.a, 2.0));
    color = mix(color, uFog.rgb, fog);

    oColor = vec4(pow(color, vec3(1.0 / 2.2)), albedo.a);
}
