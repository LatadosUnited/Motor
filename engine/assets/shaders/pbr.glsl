// Funções do PBR e do céu. Cópia das versões em C++ de motor/renderer/Pbr.h (testadas no ctest): mude as duas.

const float PI = 3.14159265358979;

float Alpha(float roughness) {
    float r = clamp(roughness, 0.02, 1.0);
    return r * r;
}

float DistributionGgx(float NdotH, float roughness) {
    float a2 = Alpha(roughness) * Alpha(roughness);
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float VisibilitySmithGgx(float NdotV, float NdotL, float roughness) {
    float a2 = Alpha(roughness) * Alpha(roughness);
    float ggxV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float ggxL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    float sum = ggxV + ggxL;
    return sum > 0.0 ? 0.5 / sum : 0.0;
}

vec3 FresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - clamp(cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel do reflexo do céu: superfícies ásperas não chegam a refletir 100% na borda.
vec3 FresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness) {
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(1.0 - clamp(cosTheta, 0.0, 1.0), 5.0);
}

vec3 BaseReflectance(vec3 albedo, float metallic, float specular) {
    return mix(vec3(0.08 * max(specular, 0.0)), albedo, clamp(metallic, 0.0, 1.0));
}

vec2 Hammersley(uint i, uint count) {
    return vec2(float(i) / float(max(count, 1u)), float(bitfieldReverse(i)) * 2.3283064365386963e-10);
}

vec3 ImportanceSampleGgx(vec2 xi, vec3 N, float roughness) {
    float a = Alpha(roughness);
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    vec3 h = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(up, N));
    vec3 b = cross(N, t);
    return normalize(t * h.x + b * h.y + N * h.z);
}

// Mesma base real de EvaluateSh9 (Pbr.cpp); os coeficientes já vêm convoluídos com o cosseno.
vec3 EvaluateSh9(vec4 sh[9], vec3 n) {
    return sh[0].rgb * 0.282095
         + sh[1].rgb * (0.488603 * n.y)
         + sh[2].rgb * (0.488603 * n.z)
         + sh[3].rgb * (0.488603 * n.x)
         + sh[4].rgb * (1.092548 * n.x * n.y)
         + sh[5].rgb * (1.092548 * n.y * n.z)
         + sh[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0))
         + sh[7].rgb * (1.092548 * n.x * n.z)
         + sh[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y));
}

vec3 GradientSky(vec3 direction, vec3 sky) {
    float y = clamp(direction.y, -1.0, 1.0);
    if (y >= 0.0) return mix(sky, sky * 0.75, pow(y, 0.6));
    return mix(sky, sky * 0.3, smoothstep(0.0, 0.25, -y));
}

vec2 EquirectUv(vec3 d) {
    return vec2(atan(d.x, -d.z) / (2.0 * PI) + 0.5, acos(clamp(d.y, -1.0, 1.0)) / PI);
}

vec3 RotateY(vec3 v, float radians) {
    float c = cos(radians), s = sin(radians);
    return vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

// Direção do texel (uv em [0,1]) de uma face do cubemap, na convenção do OpenGL (+X, -X, +Y, -Y, +Z, -Z).
vec3 CubeDirection(int face, vec2 uv) {
    vec2 c = uv * 2.0 - 1.0;
    vec3 d;
    if (face == 0) d = vec3(1.0, -c.y, -c.x);
    else if (face == 1) d = vec3(-1.0, -c.y, c.x);
    else if (face == 2) d = vec3(c.x, 1.0, c.y);
    else if (face == 3) d = vec3(c.x, -1.0, -c.y);
    else if (face == 4) d = vec3(c.x, -c.y, 1.0);
    else d = vec3(-c.x, -c.y, -1.0);
    return normalize(d);
}
