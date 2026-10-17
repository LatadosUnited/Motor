#version 450 core

// FXAA (o algoritmo da versão 3.11 de Timothy Lottes, qualidade média): acha a borda pela luminância (alfa
// da imagem), segue a borda até o fim e mistura com o vizinho do outro lado na proporção certa.

in vec2 vUv;

layout(binding = 0) uniform sampler2D uImage;
uniform vec2 uTexel;  // 1 / tamanho

out vec4 oColor;

const float kEdgeMin = 0.0312;
const float kEdgeMax = 0.125;
const float kSubpixel = 0.75;
const int kSteps = 10;
const float kStepSize[kSteps] = float[](1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 4.0, 8.0);

float Luma(vec2 uv) { return textureLod(uImage, uv, 0.0).a; }

void main() {
    vec4 center = textureLod(uImage, vUv, 0.0);
    float lumaM = center.a;
    float lumaN = Luma(vUv + vec2(0.0, uTexel.y));
    float lumaS = Luma(vUv - vec2(0.0, uTexel.y));
    float lumaE = Luma(vUv + vec2(uTexel.x, 0.0));
    float lumaW = Luma(vUv - vec2(uTexel.x, 0.0));
    float maxLuma = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float minLuma = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float range = maxLuma - minLuma;
    if (range < max(kEdgeMin, maxLuma * kEdgeMax)) {
        oColor = vec4(center.rgb, 1.0);
        return;
    }

    float lumaNW = Luma(vUv + vec2(-uTexel.x, uTexel.y));
    float lumaNE = Luma(vUv + uTexel);
    float lumaSW = Luma(vUv - uTexel);
    float lumaSE = Luma(vUv + vec2(uTexel.x, -uTexel.y));

    float edgeHorizontal = abs(lumaNW + lumaSW - 2.0 * lumaW) + 2.0 * abs(lumaN + lumaS - 2.0 * lumaM) +
                           abs(lumaNE + lumaSE - 2.0 * lumaE);
    float edgeVertical = abs(lumaNW + lumaNE - 2.0 * lumaN) + 2.0 * abs(lumaW + lumaE - 2.0 * lumaM) +
                         abs(lumaSW + lumaSE - 2.0 * lumaS);
    bool horizontal = edgeHorizontal >= edgeVertical;

    float luma1 = horizontal ? lumaS : lumaW;
    float luma2 = horizontal ? lumaN : lumaE;
    float gradient1 = luma1 - lumaM;
    float gradient2 = luma2 - lumaM;
    bool steepest1 = abs(gradient1) >= abs(gradient2);
    float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

    float stepLength = horizontal ? uTexel.y : uTexel.x;
    float lumaLocalAverage;
    if (steepest1) {
        stepLength = -stepLength;
        lumaLocalAverage = 0.5 * (luma1 + lumaM);
    } else {
        lumaLocalAverage = 0.5 * (luma2 + lumaM);
    }

    vec2 uv = vUv;
    if (horizontal) uv.y += stepLength * 0.5; else uv.x += stepLength * 0.5;
    vec2 offset = horizontal ? vec2(uTexel.x, 0.0) : vec2(0.0, uTexel.y);
    vec2 uv1 = uv - offset;
    vec2 uv2 = uv + offset;
    float lumaEnd1 = Luma(uv1) - lumaLocalAverage;
    float lumaEnd2 = Luma(uv2) - lumaLocalAverage;
    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;
    for (int i = 1; i < kSteps && !(reached1 && reached2); ++i) {
        if (!reached1) {
            uv1 -= offset * kStepSize[i];
            lumaEnd1 = Luma(uv1) - lumaLocalAverage;
            reached1 = abs(lumaEnd1) >= gradientScaled;
        }
        if (!reached2) {
            uv2 += offset * kStepSize[i];
            lumaEnd2 = Luma(uv2) - lumaLocalAverage;
            reached2 = abs(lumaEnd2) >= gradientScaled;
        }
    }

    float distance1 = horizontal ? vUv.x - uv1.x : vUv.y - uv1.y;
    float distance2 = horizontal ? uv2.x - vUv.x : uv2.y - vUv.y;
    bool closer1 = distance1 < distance2;
    float distanceFinal = min(distance1, distance2);
    float edgeLength = distance1 + distance2;
    bool centerSmaller = lumaM < lumaLocalAverage;
    bool correctVariation = ((closer1 ? lumaEnd1 : lumaEnd2) < 0.0) != centerSmaller;
    float pixelOffset = correctVariation ? -distanceFinal / edgeLength + 0.5 : 0.0;

    float lumaAverage = (2.0 * (lumaN + lumaS + lumaE + lumaW) + lumaNW + lumaNE + lumaSW + lumaSE) / 12.0;
    float subpixel1 = clamp(abs(lumaAverage - lumaM) / range, 0.0, 1.0);
    float subpixel2 = (-2.0 * subpixel1 + 3.0) * subpixel1 * subpixel1;
    float subpixelOffset = subpixel2 * subpixel2 * kSubpixel;

    float finalOffset = max(pixelOffset, subpixelOffset);
    vec2 finalUv = vUv;
    if (horizontal) finalUv.y += finalOffset * stepLength; else finalUv.x += finalOffset * stepLength;
    oColor = vec4(textureLod(uImage, finalUv, 0.0).rgb, 1.0);
}
