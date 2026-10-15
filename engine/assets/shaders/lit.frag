#version 450 core

// Blinn-Phong com uma luz direcional. Toda a iluminação é feita em espaço linear
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
    vec4 uLightDirection;
    vec4 uLightColor;
    vec4 uAmbient;
    vec4 uFog;  // rgb = cor do céu (linear), a = densidade
};

layout(binding = 0) uniform sampler2D uAlbedo;  // textura sRGB => amostrada já linear
uniform vec4 uColor;                            // sRGB
uniform float uSpecular;
uniform float uShininess;

out vec4 oColor;

void main() {
    vec4 tint = vec4(pow(uColor.rgb, vec3(2.2)), uColor.a);
    vec4 albedo = texture(uAlbedo, fs.uv) * tint;

    vec3 N = normalize(fs.normal);
    vec3 L = normalize(-uLightDirection.xyz);
    vec3 V = normalize(uCameraPosition.xyz - fs.worldPos);
    vec3 H = normalize(L + V);

    float diffuse = max(dot(N, L), 0.0);
    float specular = diffuse > 0.0 ? pow(max(dot(N, H), 0.0), uShininess) * uSpecular : 0.0;

    vec3 color = albedo.rgb * (uAmbient.rgb + diffuse * uLightColor.rgb) + specular * uLightColor.rgb;

    // Neblina simples de distância, para dar profundidade.
    float dist = length(uCameraPosition.xyz - fs.worldPos);
    float fog = 1.0 - exp(-pow(dist * uFog.a, 2.0));
    color = mix(color, uFog.rgb, fog);

    oColor = vec4(pow(color, vec3(1.0 / 2.2)), albedo.a);
}
