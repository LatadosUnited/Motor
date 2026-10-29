#version 450 core

// Água (Fase 28): reflexo do céu (o mapa pré-filtrado, com Fresnel), refração (a cópia da cor da cena feita antes da
// água, distorcida pela normal), cor pela espessura da água até o fundo (a profundidade da cena, lida direto do alvo:
// a água não grava nela nesta passada) e espuma na beira. A luz direta é só o brilho do sol nas ondas. A saída é HDR
// linear, como o lit.frag. SCENE_MSAA (variante do Renderer): a profundidade tem amostras; lê a primeira.

in VS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} fs;

#include "frame.glsl"
#include "pbr.glsl"
#include "lights.glsl"

layout(binding = 7) uniform samplerCube uPrefiltered;
layout(binding = 14) uniform sampler2D uRefractColor;  // a cena antes da água (luz linear)
#ifdef SCENE_MSAA
layout(binding = 15) uniform sampler2DMS uSceneDepth;  // a profundidade da cena (valores do depth buffer)
#else
layout(binding = 15) uniform sampler2D uSceneDepth;
#endif

uniform vec4 uShallow;  // rgb = cor rasa (sRGB), a = metros até o fundo sumir
uniform vec4 uDeep;     // rgb = cor funda (sRGB), a = distorção da refração
uniform vec4 uWater;    // x = reflexo, y = metros de espuma, z = tempo, w = comprimento da onda

out vec4 oColor;

// Ponto da cena (relativo à câmera) no pixel `uv` da cópia.
vec3 ScenePoint(vec2 uv) {
    ivec2 texel = clamp(ivec2(uv * uViewport.xy), ivec2(0), ivec2(uViewport.xy) - 1);
    float depth = texelFetch(uSceneDepth, texel, 0).r;  // com MSAA, a amostra 0; sem, o nível 0
    vec4 p = uInvViewProjection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

// Ondulações pequenas que as ondas da malha não têm: derivadas de senos cruzados, rolando com o tempo.
vec2 Ripples(vec2 p, float t, float scale) {
    vec2 d = vec2(0.0);
    d += vec2(cos(p.x * 1.7 + p.y * 0.6 + t * 1.9), cos(p.y * 1.3 - p.x * 0.4 + t * 1.6)) * 0.5;
    d += vec2(cos(p.x * 3.1 - p.y * 2.3 - t * 2.7), cos(p.y * 3.7 + p.x * 1.9 + t * 2.3)) * 0.25;
    d += vec2(cos(p.x * 6.3 + p.y * 5.1 + t * 3.9), cos(p.y * 7.1 - p.x * 4.3 - t * 3.3)) * 0.12;
    return d * scale;
}

void main() {
    vec3 N = normalize(fs.normal);
    // As ondulações somem com a distância (senão viram ruído) e quando a onda configurada é pequena.
    float dist = length(fs.worldPos);
    // Longe, a neblina já cobre a água: nada do resto aparece.
    if (exp(-pow(dist * uFog.a, 2.0)) < 0.002) {
        oColor = vec4(uFog.rgb, 1.0);
        return;
    }
    float rippleScale = 0.08 * clamp(1.0 - dist / 150.0, 0.0, 1.0);
    vec2 ripple = Ripples(fs.uv * (8.0 / max(uWater.w, 1.0)) * 4.0, uWater.z, rippleScale);
    N = normalize(N + vec3(ripple.x, 0.0, ripple.y));
    vec3 V = -fs.worldPos / max(dist, 1e-4);  // a câmera está na origem
    bool below = !gl_FrontFacing || V.y < 0.0;
    if (below) N = -N;

    // Refração: o fundo pela cópia, deslocado pela normal; onde o ponto deslocado está na frente da água (um objeto
    // em cima dela), sem deslocamento.
    vec2 screen = gl_FragCoord.xy * uViewport.zw;
    vec2 uv = clamp(screen + N.xz * uDeep.a, vec2(0.001), vec2(0.999));
    vec3 scene = ScenePoint(uv);
    if (length(scene) < dist) {
        uv = screen;
        scene = ScenePoint(uv);
    }
    vec3 refracted = texture(uRefractColor, uv).rgb;
    float thickness = max(length(scene) - dist, 0.0);  // metros de água no raio de visão
    float depthBelow = max(fs.worldPos.y - scene.y, 0.0);  // metros na vertical até o fundo

    // Luz que chega na água: céu (irradiância de cima), a ambiente e as direcionais.
    vec3 light = max(EvaluateSh9(uSh, vec3(0.0, 1.0, 0.0)), vec3(0.0)) / PI + uAmbient.rgb;
    vec3 sun = vec3(0.0);
    for (int i = 0; i < uLightCount.x; ++i) {
        LightData l = uLights[i];
        if (int(l.directionType.w + 0.5) != kDirectional) continue;
        vec3 L = -l.directionType.xyz;
        light += l.color.rgb * max(L.y, 0.0);
        // Brilho do sol nas ondas: lobo bem estreito (a água é quase um espelho).
        vec3 H = normalize(L + V);
        sun += l.color.rgb * pow(max(dot(N, H), 0.0), 600.0) * 6.0 * step(0.0, dot(N, L));
    }

    vec3 shallow = pow(uShallow.rgb, vec3(2.2));
    vec3 deep = pow(uDeep.rgb, vec3(2.2));
    float fade = max(uShallow.a, 0.05);
    float transmit = exp(-thickness / fade * 2.0);  // quanto do fundo ainda passa
    vec3 scatter = mix(shallow, deep, smoothstep(0.0, 1.0, thickness / (fade * 1.5))) * light;
    vec3 body = mix(scatter, refracted * mix(shallow * 1.6, vec3(1.0), transmit), transmit);

    if (below) {
        // Visto de baixo: só o fundo (a superfície) tingido, sem reflexo do céu.
        oColor = vec4(mix(deep * light, refracted, 0.6), 1.0);
        return;
    }

    // Reflexo do céu: Fresnel de Schlick com f0 da água (0,02). O raio refletido nunca aponta para baixo.
    float NdotV = max(dot(N, V), 0.0);
    float fresnel = (0.02 + 0.98 * pow(1.0 - NdotV, 5.0)) * clamp(uWater.x, 0.0, 1.0);
    vec3 R = reflect(-V, N);
    R.y = abs(R.y);
    vec3 reflected = textureLod(uPrefiltered, RotateY(R, -uSky.z), 0.0).rgb * uSky.y;
    vec3 color = mix(body, reflected, fresnel) + sun * fresnel * 25.0;

    // Espuma onde a água fica rasa (beira, objetos), recortada por um padrão que anda.
    if (uWater.y > 0.0) {
        float edge = 1.0 - smoothstep(0.0, uWater.y, depthBelow);
        float pattern = 0.5 + 0.5 * sin(fs.uv.x * 2.3 + uWater.z * 0.9) * sin(fs.uv.y * 2.9 - uWater.z * 0.7);
        float foam = edge * smoothstep(0.25, 0.75, edge * 0.7 + pattern * 0.6);
        color = mix(color, light * 0.9, foam * 0.85);
    }

    // Neblina de distância, como no lit.frag.
    float fog = 1.0 - exp(-pow(dist * uFog.a, 2.0));
    color = mix(color, uFog.rgb, fog);
    oColor = vec4(color, 1.0);
}
