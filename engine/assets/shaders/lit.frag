#version 450 core

// PBR de metal/rugosidade (Cook-Torrance com GGX) com N luzes, sombras (cascatas da direcional, cubos das
// pontuais/spots) e a luz do céu (irradiância em harmônicos esféricos + reflexo pré-filtrado). A saída é a
// luz em HDR linear; tone mapping e gama ficam para o pós-processamento (composite.frag).

in VS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
#ifdef TERRAIN
    flat vec2 corner;
#endif
} fs;

#include "frame.glsl"
#include "pbr.glsl"
#include "lights.glsl"

// Igual ao ShadowData do Renderer.cpp.
layout(std140, binding = 3) uniform Shadows {
    mat4 uCascadeMatrices[4];
    vec4 uCascadeSplits;  // distância (na vista) do fim de cada cascata
    vec4 uCascadeTexels;  // tamanho do texel de cada cascata no mundo
    vec4 uShadowParams;   // x = nº de cascatas, y = 1 / resolução das cascatas, z = 1 / resolução dos cubos, w = distância das sombras
};

// Filtro das cascatas pela qualidade da direcional (variante do Renderer): 0 baixa, 1 média, 2 alta. Pela uniform, com
// if, a Intel compilava os três caminhos e a cena ficava ~3 ms mais lenta na referência.
#ifndef SHADOW_FILTER
#define SHADOW_FILTER 2
#endif

layout(binding = 0) uniform sampler2D uAlbedo;        // sRGB => amostrada já linear
layout(binding = 1) uniform sampler2D uNormalMap;     // linear
layout(binding = 2) uniform sampler2D uMetalRoughMap; // linear: G = rugosidade, B = metal
layout(binding = 3) uniform sampler2D uOcclusionMap;  // linear: R
layout(binding = 4) uniform sampler2D uEmissiveMap;   // sRGB
layout(binding = 5) uniform sampler2DArrayShadow uCascades;
layout(binding = 6) uniform samplerCubeArrayShadow uPointShadows;
layout(binding = 7) uniform samplerCube uPrefiltered;
layout(binding = 8) uniform sampler2D uBrdfLut;

#ifdef TERRAIN
// Terreno com camadas (Fase 28, motor::TerrainLayers): pesos RGBA de todos os blocos (uma camada do array por bloco,
// uma amostra por vértice), o índice bloco -> camada e as 4 texturas. fs.uv são os metros do terreno.
layout(binding = 9) uniform sampler2DArray uTerrainWeights;  // linear
layout(binding = 10) uniform sampler2D uLayer0;              // sRGB
layout(binding = 11) uniform sampler2D uLayer1;
layout(binding = 12) uniform sampler2D uLayer2;
layout(binding = 13) uniform sampler2D uLayer3;
layout(binding = 14) uniform sampler2D uTerrainIndex;        // R = camada do bloco (-1 = sem pesos)
uniform mat4 uLayerColors;   // coluna i = cor (sRGB) da camada i
uniform vec4 uLayerTiling;   // metros por repetição da textura de cada camada
uniform vec4 uTerrainGrid;   // x = tamanho do bloco (metros), y = amostras por lado, zw = primeiro bloco do índice

vec3 LayerColor(int i, vec4 texel) {
    return pow(uLayerColors[i].rgb, vec3(2.2)) * texel.rgb;
}

vec3 TerrainAlbedo() {
    // O bloco do triângulo pelo canto (igual nos 3 vértices) e a camada dele no array.
    ivec2 key = ivec2(round(fs.corner / uTerrainGrid.x));
    ivec2 cell = clamp(key - ivec2(uTerrainGrid.zw), ivec2(0), textureSize(uTerrainIndex, 0) - 1);
    float layer = texelFetch(uTerrainIndex, cell, 0).r;
    // A amostra j do bloco fica no centro do texel j: (j + 0,5) / amostras. A leitura sai sempre (sem if: as derivadas
    // do mipmap); bloco sem camada fica com a camada 1.
    vec2 local = (fs.uv - fs.corner) / uTerrainGrid.x * (uTerrainGrid.y - 1.0);
    vec4 w = texture(uTerrainWeights, vec3((local + 0.5) / uTerrainGrid.y, max(layer, 0.0)));
    if (layer < 0.0) w = vec4(1.0, 0.0, 0.0, 0.0);
    w /= max(dot(w, vec4(1.0)), 1e-4);
    // As 4 leituras sempre: um if pelo peso deixaria a escolha do mipmap indefinida (derivadas em fluxo divergente).
    vec4 tiling = max(uLayerTiling, vec4(1e-3));
    return w.x * LayerColor(0, texture(uLayer0, fs.uv / tiling.x)) + w.y * LayerColor(1, texture(uLayer1, fs.uv / tiling.y)) +
           w.z * LayerColor(2, texture(uLayer2, fs.uv / tiling.z)) + w.w * LayerColor(3, texture(uLayer3, fs.uv / tiling.w));
}
#endif

uniform vec4 uColor;     // sRGB
uniform vec4 uPbr;       // x = metal, y = rugosidade, z = specular, w = força do mapa de normal (0 = sem mapa)
uniform vec4 uEmissive;  // rgb = emissão (linear, já com a intensidade), a = força da oclusão
uniform vec4 uAlphaMode; // x = 0 opaco, 1 recorte, 2 transparente; y = limite do recorte; z = mapas que o material tem

// Bits de uAlphaMode.z (motor::kMapAlbedo...): mapa que falta vale branco. Com NO_MAPS (materiais sem nenhum mapa,
// variante do Renderer) as leituras nem entram no shader: o if pela uniform não evita o custo no driver da Intel.
const int kMapAlbedo = 1;
const int kMapMetallicRoughness = 2;
const int kMapOcclusion = 4;
const int kMapEmissive = 8;

out vec4 oColor;

float RangeAttenuation(float dist, float range) {
    float ratio = dist / range;
    float window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    return window * window / (1.0 + 4.0 * ratio * ratio);
}

// Base tangente pelas derivadas da tela (sem tangentes nos vértices; vale também para malhas com pele).
mat3 CotangentFrame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p), dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
    return mat3(T * invmax, B * invmax, N);
}

float SampleCascade(int c, vec3 worldPos, vec3 N) {
    vec3 offsetPos = worldPos + N * uCascadeTexels[c] * 1.5;
    vec4 p = uCascadeMatrices[c] * vec4(offsetPos, 1.0);
    vec3 proj = p.xyz / p.w * 0.5 + 0.5;
    if (proj.z >= 1.0) return 1.0;
    float texel = uShadowParams.y;
    // A comparação com filtro linear já mistura 2x2 texels por leitura. Qualidade baixa: uma leitura; média: 4 a meio
    // texel (cobre 3x3); alta: 9 (4x4). Na HD 530 as 9 custavam ~0,5 ms a mais que as 4 na referência.
#if SHADOW_FILTER == 0
    return texture(uCascades, vec4(proj.xy, float(c), proj.z));
#elif SHADOW_FILTER == 1
    float sum = 0.0;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            sum += texture(uCascades, vec4(proj.xy + (vec2(x, y) - 0.5) * texel, float(c), proj.z));
        }
    }
    return sum * 0.25;
#else
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            sum += texture(uCascades, vec4(proj.xy + vec2(x, y) * texel, float(c), proj.z));
        }
    }
    return sum / 9.0;
#endif
}

float DirectionalShadow(vec3 worldPos, vec3 N) {
    int count = int(uShadowParams.x + 0.5);
    if (count == 0) return 1.0;
    float depth = -(uView * vec4(worldPos, 1.0)).z;
    float maxDist = uShadowParams.w;
    if (depth >= maxDist) return 1.0;
    int c = 0;
    while (c < count - 1 && depth > uCascadeSplits[c]) ++c;
    float shadow = SampleCascade(c, worldPos, N);
    // Qualidade alta: mistura com a próxima cascata perto da divisa (sem degrau visível). Ler duas cascatas nessa faixa
    // custava ~1 ms na referência (HD 530); nas outras qualidades o degrau fica.
#if SHADOW_FILTER == 2
    float start = c == 0 ? 0.0 : uCascadeSplits[c - 1];
    float band = (uCascadeSplits[c] - start) * 0.15;
    float t = (depth - (uCascadeSplits[c] - band)) / max(band, 1e-4);
    if (c < count - 1 && t > 0.0) shadow = mix(shadow, SampleCascade(c + 1, worldPos, N), clamp(t, 0.0, 1.0));
#endif
    // Some aos poucos no fim da distância das sombras.
    return mix(shadow, 1.0, clamp((depth - maxDist * 0.85) / (maxDist * 0.15), 0.0, 1.0));
}

float PointShadow(int layer, vec3 worldPos, vec3 N, vec3 lightPos, float range) {
    vec3 toFrag = worldPos - lightPos;
    float dist = length(toFrag);
    float texelAngle = 2.0 * uShadowParams.z;  // largura de um texel do cubo, por metro de distância
    vec3 d = toFrag + N * dist * texelAngle * 1.5;
    float ref = length(d) / range - 0.002;
    vec3 dn = normalize(d);
    vec3 a = normalize(cross(dn, abs(dn.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 b = cross(dn, a);
    float r = length(d) * texelAngle * 1.25;
    float sum = texture(uPointShadows, vec4(d, float(layer)), ref);
    sum += texture(uPointShadows, vec4(d + (a + b) * r, float(layer)), ref);
    sum += texture(uPointShadows, vec4(d + (a - b) * r, float(layer)), ref);
    sum += texture(uPointShadows, vec4(d + (-a + b) * r, float(layer)), ref);
    sum += texture(uPointShadows, vec4(d + (-a - b) * r, float(layer)), ref);
    return sum / 5.0;
}

void main() {
    int maps = int(uAlphaMode.z + 0.5);
    vec4 tint = vec4(pow(uColor.rgb, vec3(2.2)), uColor.a);
    vec4 albedo = tint;
#ifdef TERRAIN
    albedo = vec4(TerrainAlbedo(), 1.0);  // a cor e a textura do material já são a camada 1
    // Longe, a neblina já cobre o terreno: sem a luz (num terreno de 1 km é uma faixa grande da tela). Depois daqui só
    // há leituras sem mipmap (sombra, tabela da BRDF) ou com o nível dado: o return fora de ordem não as atrapalha.
    float fogDistance = length(uCameraPosition.xyz - fs.worldPos);
    if (exp(-pow(fogDistance * uFog.a, 2.0)) < 0.002) {
        oColor = vec4(uFog.rgb, 1.0);
        return;
    }
#elif !defined(NO_MAPS)
    if ((maps & kMapAlbedo) != 0) albedo *= texture(uAlbedo, fs.uv);
#endif
    if (uAlphaMode.x == 1.0 && albedo.a < uAlphaMode.y) discard;  // recorte (folhas, grades)
    vec4 mr = vec4(1.0);
#ifndef NO_MAPS
    if ((maps & kMapMetallicRoughness) != 0) mr = texture(uMetalRoughMap, fs.uv);
#endif
    float metallic = clamp(uPbr.x * mr.b, 0.0, 1.0);
    float roughness = clamp(uPbr.y * mr.g, 0.045, 1.0);
    float occlusion = 1.0;
#ifndef NO_MAPS
    if ((maps & kMapOcclusion) != 0) occlusion = mix(1.0, texture(uOcclusionMap, fs.uv).r, uEmissive.a);
#endif

    vec3 geometricN = normalize(fs.normal);
    if (!gl_FrontFacing) geometricN = -geometricN;  // material de duas faces visto por trás
    vec3 N = geometricN;
#ifndef NO_MAPS
    if (uPbr.w > 0.0) {
        vec3 t = texture(uNormalMap, fs.uv).xyz * 2.0 - 1.0;
        t.xy *= uPbr.w;
        N = normalize(CotangentFrame(geometricN, fs.worldPos, fs.uv) * t);
    }
#endif
    vec3 V = normalize(uCameraPosition.xyz - fs.worldPos);
    float NdotV = max(dot(N, V), 1e-4);
    vec3 f0 = BaseReflectance(albedo.rgb, metallic, uPbr.z);
    vec3 diffuseColor = albedo.rgb * (1.0 - metallic);

    vec3 direct = vec3(0.0);
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
        float NdotL = dot(N, L);
        if (NdotL <= 0.0) continue;

        if (light.cone.w > 0.5) {
            attenuation *= DirectionalShadow(fs.worldPos, geometricN);
        } else if (light.cone.z >= 0.0) {
            attenuation *= PointShadow(int(light.cone.z + 0.5), fs.worldPos, geometricN, light.positionRange.xyz,
                                       light.positionRange.w);
        }
        if (attenuation <= 0.0) continue;

#ifdef TERRAIN
        // Terreno: só a parte difusa da luz direta. O brilho do GGX num chão fosco quase não aparece e custava ~1,6 ms
        // no terreno de 1 km da HD 530; o reflexo do céu (abaixo) continua.
        direct += diffuseColor * light.color.rgb * attenuation * NdotL;
        continue;
#endif
        vec3 H = normalize(L + V);
        float NdotH = max(dot(N, H), 0.0);
        vec3 F = FresnelSchlick(max(dot(V, H), 0.0), f0);
        vec3 specular = DistributionGgx(NdotH, roughness) * VisibilitySmithGgx(NdotV, NdotL, roughness) * F;
        // A luz de intensidade 1 ilumina de frente um branco com 1 (como no Blinn-Phong de antes): o 1/PI do
        // difuso sai, e o especular ganha o PI.
        vec3 diffuse = (1.0 - F) * diffuseColor;
        direct += (diffuse + specular * PI) * light.color.rgb * attenuation * NdotL;
    }

    // Luz do céu: difusa (irradiância) e reflexo (mapa pré-filtrado com a tabela da BRDF).
    vec3 irradiance = max(EvaluateSh9(uSh, N), vec3(0.0));
    vec3 Fr = FresnelSchlickRoughness(NdotV, f0, roughness);
    vec3 R = RotateY(reflect(-V, N), -uSky.z);
    vec3 prefiltered = textureLod(uPrefiltered, R, roughness * uSky.w).rgb * uSky.y;
    vec2 brdf = texture(uBrdfLut, vec2(NdotV, roughness)).rg;
    vec3 ambient = (1.0 - Fr) * diffuseColor * (irradiance / PI + uAmbient.rgb) + prefiltered * (f0 * brdf.x + brdf.y);

    vec3 emissive = uEmissive.rgb;
#ifndef NO_MAPS
    if ((maps & kMapEmissive) != 0) emissive *= texture(uEmissiveMap, fs.uv).rgb;
#endif
    vec3 color = direct + ambient * occlusion + emissive;

    // Neblina de distância: a cor é a do horizonte do céu.
    float dist = length(uCameraPosition.xyz - fs.worldPos);
    float fog = 1.0 - exp(-pow(dist * uFog.a, 2.0));
    color = mix(color, uFog.rgb, fog);

    oColor = vec4(color, uAlphaMode.x == 2.0 ? albedo.a : 1.0);  // só o transparente mistura
}
