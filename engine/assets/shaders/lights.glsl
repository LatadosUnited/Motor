// Bloco "Lights" (std140), igual ao LightsData do Renderer.cpp: muda um, muda o outro. Usado pelo lit.frag e pela
// água (water.frag).
// Mesmo tamanho que kMaxLights (motor/renderer/Light.h).
const int kMaxLights = 32;
const int kDirectional = 0;
const int kPoint = 1;
const int kSpot = 2;

struct LightData {
    vec4 positionRange;  // xyz = posição, w = alcance
    vec4 directionType;  // xyz = direção (normalizada), w = tipo
    vec4 color;          // rgb = cor * intensidade
    vec4 cone;           // x = cos(ângulo interno), y = cos(ângulo externo), z = camada do cubo de sombra (-1 = sem), w = 1: usa as cascatas
};

layout(std140, binding = 1) uniform Lights {
    ivec4 uLightCount;  // x = número de luzes
    LightData uLights[kMaxLights];
};
