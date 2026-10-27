// Renderização avançada (Fase 7), sem GPU: matemática das cascatas e do recorte, BRDF e amostragem, luz do céu
// (harmônicos esféricos, degradê, imagem equirretangular), imagens HDR e os formatos novos (ambiente da cena,
// sombra nas luzes, migração do brilho para rugosidade).
#include "TestUtil.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/core/Paths.h>
#include <motor/renderer/DrawOrder.h>
#include <motor/renderer/Pbr.h>
#include <motor/renderer/ShadowMath.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneSerializer.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

using namespace motor;
namespace fs = std::filesystem;

namespace {
bool Near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) <= eps; }
bool Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) { return glm::length(a - b) <= eps; }

glm::vec3 Project(const glm::mat4& m, const glm::vec3& p) {
    const glm::vec4 c = m * glm::vec4(p, 1.0f);
    return glm::vec3(c) / c.w;
}

bool InsideClip(const glm::vec3& ndc, float eps = 1e-4f) {
    return std::abs(ndc.x) <= 1.0f + eps && std::abs(ndc.y) <= 1.0f + eps && std::abs(ndc.z) <= 1.0f + eps;
}

// Arquivo Radiance .hdr mínimo (sem RLE: largura < 8), pixels em RGBE, linha de cima primeiro.
void WriteHdr(const fs::path& file, int width, int height, const std::vector<std::array<uint8_t, 4>>& rgbe) {
    std::ofstream out(file, std::ios::binary);
    out << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " << height << " +X " << width << "\n";
    for (const auto& p : rgbe) out.write(reinterpret_cast<const char*>(p.data()), 4);
}
} // namespace

int main(int argc, char** argv) {
    const fs::path root = TestDir(argc, argv, "test_render_tmp");
    std::string error;

    SECTION("Cascatas: divisões crescentes, a última no fim da distância das sombras");
    {
        const std::vector<float> splits = CascadeSplits(0.1f, 60.0f, 4, 0.8f);
        CHECK(splits.size() == 4);
        bool increasing = splits.size() == 4 && splits[0] > 0.1f;
        for (size_t i = 1; i < splits.size(); ++i) increasing = increasing && splits[i] > splits[i - 1];
        CHECK(increasing);
        CHECK(splits.size() == 4 && splits.back() == 60.0f);
        // lambda 0 = uniforme; lambda 1 = logarítmica (cada cascata cobre a mesma proporção).
        const std::vector<float> uniform = CascadeSplits(1.0f, 101.0f, 4, 0.0f);
        CHECK(Near(uniform[0], 26.0f, 1e-3f) && Near(uniform[1], 51.0f, 1e-3f));
        const std::vector<float> log = CascadeSplits(1.0f, 10000.0f, 4, 1.0f);
        CHECK(Near(log[0], 10.0f, 1e-2f) && Near(log[1], 100.0f, 1e-1f) && Near(log[2], 1000.0f, 1.0f));
        CHECK(CascadeSplits(0.1f, 50.0f, 1).size() == 1);
    }

    SECTION("Cascatas: o pedaço do frustum da câmera cabe inteiro no mapa da luz");
    Camera camera;
    camera.position = {3.0f, 2.0f, 8.0f};
    camera.yaw = -110.0f;
    camera.pitch = -20.0f;
    camera.aspect = 16.0f / 9.0f;
    const glm::vec3 sun = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.35f));
    {
        const std::vector<float> splits = CascadeSplits(camera.nearPlane, 60.0f, 4);
        bool allInside = true;
        for (size_t c = 0; c < splits.size(); ++c) {
            const float nearDist = c == 0 ? camera.nearPlane : splits[c - 1];
            const Cascade cascade = ComputeCascade(camera, nearDist, splits[c], sun, 2048);
            for (const glm::vec3& corner : FrustumSliceCorners(camera, nearDist, splits[c])) {
                allInside = allInside && InsideClip(Project(cascade.viewProjection, corner));
            }
        }
        CHECK(allInside);
        // Um objeto entre o sol e o pedaço (até casterDepth antes) ainda entra no mapa.
        const Cascade near = ComputeCascade(camera, camera.nearPlane, splits[0], sun, 2048, 50.0f);
        const glm::vec3 caster = near.center - sun * (near.radius + 40.0f);
        CHECK(InsideClip(Project(near.viewProjection, caster)));
        CHECK(near.texelSize > 0.0f && Near(near.texelSize, 2.0f * near.radius / 2048.0f, 1e-6f));
    }

    SECTION("Cascatas reaproveitadas: a desenhada com folga cobre a câmera que andou pouco");
    {
        const Cascade drawn = ComputeCascade(camera, 20.0f, 60.0f, sun, 2048, 50.0f, 1.15f);
        CHECK(CascadeCovers(drawn, ComputeCascade(camera, 20.0f, 60.0f, sun, 2048)));
        Camera moved = camera;
        moved.position += glm::vec3(0.5f, 0.0f, -0.4f);
        const Cascade near = ComputeCascade(moved, 20.0f, 60.0f, sun, 2048);
        CHECK(CascadeCovers(drawn, near));
        bool inside = true;  // o pedaço da câmera que andou cabe no mapa já desenhado
        for (const glm::vec3& corner : FrustumSliceCorners(moved, 20.0f, 60.0f)) {
            inside = inside && InsideClip(Project(drawn.viewProjection, corner));
        }
        CHECK(inside);
        moved.position += glm::vec3(30.0f, 0.0f, 0.0f);
        CHECK(!CascadeCovers(drawn, ComputeCascade(moved, 20.0f, 60.0f, sun, 2048)));
        Camera turned = camera;
        turned.yaw += 90.0f;
        CHECK(!CascadeCovers(drawn, ComputeCascade(turned, 20.0f, 60.0f, sun, 2048)));
        CHECK(!CascadeCovers(Cascade{}, near));  // nada desenhado ainda
    }

    SECTION("Cascatas: andar ou girar a câmera não muda o tamanho e só anda em texels inteiros");
    {
        const Cascade a = ComputeCascade(camera, 5.0f, 20.0f, sun, 1024);
        Camera moved = camera;
        moved.position += glm::vec3(0.0137f, 0.0f, -0.0213f);
        const Cascade b = ComputeCascade(moved, 5.0f, 20.0f, sun, 1024);
        Camera turned = camera;
        turned.yaw += 37.0f;
        const Cascade c = ComputeCascade(turned, 5.0f, 20.0f, sun, 1024);
        CHECK(a.radius == b.radius && a.radius == c.radius);
        // Mesma orientação: a diferença é uma translação em NDC múltipla de 2/resolução (um texel).
        const glm::vec3 shift = Project(b.viewProjection, glm::vec3(0.0f)) - Project(a.viewProjection, glm::vec3(0.0f));
        const float tx = shift.x * 1024.0f / 2.0f, ty = shift.y * 1024.0f / 2.0f;
        CHECK(Near(tx, std::round(tx), 1e-2f) && Near(ty, std::round(ty), 1e-2f));
    }

    SECTION("Recorte: caixa transformada e frustum");
    {
        const Aabb unit{glm::vec3(-0.5f), glm::vec3(0.5f)};
        glm::mat4 m(1.0f);
        m[3] = glm::vec4(10.0f, 0.0f, 0.0f, 1.0f);
        m[0] *= 2.0f;
        const Aabb moved = TransformAabb(unit, m);
        CHECK(Near(moved.min, glm::vec3(9.0f, -0.5f, -0.5f)) && Near(moved.max, glm::vec3(11.0f, 0.5f, 0.5f)));

        Camera cam;
        cam.position = glm::vec3(0.0f);
        cam.yaw = -90.0f;  // olhando para -Z
        cam.pitch = 0.0f;
        const Frustum f = Frustum::FromMatrix(cam.Projection() * cam.View());
        CHECK(f.Intersects({glm::vec3(-1.0f, -1.0f, -11.0f), glm::vec3(1.0f, 1.0f, -9.0f)}));   // na frente
        CHECK(!f.Intersects({glm::vec3(-1.0f, -1.0f, 9.0f), glm::vec3(1.0f, 1.0f, 11.0f)}));    // atrás
        CHECK(!f.Intersects({glm::vec3(-1.0f, -1.0f, -1000.0f), glm::vec3(1.0f, 1.0f, -900.0f)}));  // depois do longe
        CHECK(!f.Intersects({glm::vec3(50.0f, -1.0f, -11.0f), glm::vec3(52.0f, 1.0f, -9.0f)}));  // ao lado
        CHECK(f.IntersectsSphere(glm::vec3(0.0f, 0.0f, 3.0f), 3.5f) && !f.IntersectsSphere(glm::vec3(0.0f, 0.0f, 3.0f), 2.5f));
    }

    SECTION("Sombra de pontual: cada face do cubo olha para a sua direção");
    {
        const glm::vec3 lamp{1.0f, 2.0f, 3.0f};
        const glm::vec3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        const glm::mat4 projection = CubeFaceProjection(0.05f, 10.0f);
        bool centered = true;
        for (int face = 0; face < 6; ++face) {
            const glm::vec3 ndc = Project(projection * CubeFaceView(lamp, face), lamp + dirs[face] * 4.0f);
            centered = centered && Near(ndc.x, 0.0f) && Near(ndc.y, 0.0f) && ndc.z > -1.0f && ndc.z < 1.0f;
        }
        CHECK(centered);
        // Convenção do OpenGL na face +X: o +Z do mundo fica à esquerda (s menor) e o +Y embaixo (t menor).
        const glm::vec3 p = Project(projection * CubeFaceView(lamp, 0), lamp + glm::vec3(4.0f, 1.0f, 1.0f));
        CHECK(p.x < 0.0f && p.y < 0.0f);
        CHECK(ShadowResolution(LightType::Directional, ShadowQuality::Medium) == 2048);
        CHECK(ShadowResolution(LightType::Point, ShadowQuality::High) == 1024);
    }

    SECTION("Sombra de spot: só as faces do cubo que o cone alcança");
    {
        auto faces = [](const glm::vec3& direction, float angle) {
            int mask = 0;
            for (int face = 0; face < 6; ++face) mask |= SpotCoversCubeFace(direction, angle, face) ? 1 << face : 0;
            return mask;
        };
        CHECK(faces({0.0f, -1.0f, 0.0f}, 35.0f) == 1 << 3);              // para baixo: só -Y
        CHECK((faces({1.0f, -1.0f, 0.0f}, 30.0f) & 0xF) == ((1 << 0) | (1 << 3)));  // na diagonal: +X e -Y, sem -X e +Y
        CHECK(faces({0.0f, -1.0f, 0.0f}, 90.0f) == 0x3F);                 // meia esfera: encosta em todas
        CHECK(faces({0.0f, 0.0f, 0.0f}, 35.0f) == 0x3F);                  // sem direção: todas
        // Toda direção dentro do cone cai numa face desenhada (a do maior eixo).
        bool covered = true;
        const glm::vec3 axis = glm::normalize(glm::vec3(0.3f, -1.0f, 0.6f));
        const glm::vec3 side = glm::normalize(glm::cross(axis, glm::vec3(1.0f, 0.0f, 0.0f)));
        const glm::vec3 up = glm::cross(axis, side);
        for (int i = 0; i < 64; ++i) {
            const float spin = glm::radians(static_cast<float>(i) * 5.625f);
            const glm::vec3 d = axis * std::cos(glm::radians(40.0f)) +
                                (side * std::cos(spin) + up * std::sin(spin)) * std::sin(glm::radians(40.0f));
            const glm::vec3 a = glm::abs(d);
            const int face = a.x >= a.y && a.x >= a.z ? (d.x > 0 ? 0 : 1) : a.y >= a.z ? (d.y > 0 ? 2 : 3) : (d.z > 0 ? 4 : 5);
            covered = covered && SpotCoversCubeFace(axis, 40.0f, face);
        }
        CHECK(covered);
    }

    SECTION("BRDF: GGX normalizada, Fresnel e reflexo de base");
    {
        // ∫ D(h) (n·h) dω = 1 (a distribuição das micro-facetas projetada soma 1).
        for (float roughness : {0.3f, 0.6f, 1.0f}) {
            double sum = 0.0;
            const int n = 2000;
            for (int i = 0; i < n; ++i) {
                const float theta = (static_cast<float>(i) + 0.5f) / static_cast<float>(n) * kPi * 0.5f;
                sum += DistributionGgx(std::cos(theta), roughness) * std::cos(theta) * std::sin(theta) *
                       (kPi * 0.5f / n) * 2.0f * kPi;
            }
            CHECK(Near(static_cast<float>(sum), 1.0f, 0.02f));
        }
        CHECK(Near(FresnelSchlick(1.0f, glm::vec3(0.04f)), glm::vec3(0.04f)));
        CHECK(Near(FresnelSchlick(0.0f, glm::vec3(0.04f)), glm::vec3(1.0f)));
        CHECK(Near(BaseReflectance(glm::vec3(0.9f, 0.1f, 0.1f), 0.0f, 0.5f), glm::vec3(0.04f)));
        CHECK(Near(BaseReflectance(glm::vec3(0.9f, 0.1f, 0.1f), 1.0f, 0.5f), glm::vec3(0.9f, 0.1f, 0.1f)));
        CHECK(VisibilitySmithGgx(0.5f, 0.5f, 0.5f) > 0.0f && VisibilitySmithGgx(1.0f, 1.0f, 0.02f) <= 0.25f + 1e-4f);
    }

    SECTION("Amostragem: Hammersley e GGX por importância");
    {
        CHECK(Hammersley(0, 16) == glm::vec2(0.0f, 0.0f));
        CHECK(Near(Hammersley(1, 16).y, 0.5f) && Near(Hammersley(2, 16).y, 0.25f) && Near(Hammersley(3, 16).y, 0.75f));
        const glm::vec3 N = glm::normalize(glm::vec3(0.3f, 0.8f, -0.2f));
        bool hemisphere = true, unit = true;
        float meanCos = 0.0f;
        for (uint32_t i = 0; i < 256; ++i) {
            const glm::vec3 h = ImportanceSampleGgx(Hammersley(i, 256), N, 0.4f);
            unit = unit && Near(glm::length(h), 1.0f, 1e-4f);
            hemisphere = hemisphere && glm::dot(h, N) >= 0.0f;
            meanCos += glm::dot(h, N) / 256.0f;
        }
        CHECK(unit && hemisphere);
        CHECK(meanCos > 0.85f);  // rugosidade 0.4: os meio-vetores ficam perto da normal
        const glm::vec3 mirror = ImportanceSampleGgx({0.3f, 0.7f}, N, 0.0f);
        CHECK(glm::dot(mirror, N) > 0.999f);  // espelho: sempre a normal
    }

    SECTION("Tabela da BRDF (split sum)");
    {
        const glm::vec2 front = IntegrateBrdf(1.0f, 0.02f, 512);
        CHECK(Near(front.x + front.y, 1.0f, 0.02f) && front.y < 0.01f);  // espelho de frente: reflete o F0 inteiro
        bool bounded = true;
        for (float nv : {0.05f, 0.3f, 0.7f, 1.0f}) {
            for (float r : {0.05f, 0.5f, 1.0f}) {
                const glm::vec2 v = IntegrateBrdf(nv, r, 256);
                bounded = bounded && v.x >= 0.0f && v.y >= 0.0f && v.x + v.y <= 1.0f + 1e-3f;
            }
        }
        CHECK(bounded);
        CHECK(IntegrateBrdf(0.1f, 0.1f, 256).y > IntegrateBrdf(1.0f, 0.1f, 256).y);  // Fresnel na borda
        CHECK(IntegrateBrdf(0.7f, 1.0f, 256).x < IntegrateBrdf(0.7f, 0.1f, 256).x);   // áspero perde energia
        const std::vector<glm::vec2> lut = BuildBrdfLut(8, 64);
        CHECK(lut.size() == 64);
        CHECK(lut.size() == 64 && Near(lut[7].x + lut[7].y, IntegrateBrdf(15.0f / 16.0f, 1.0f / 16.0f, 64).x +
                                                               IntegrateBrdf(15.0f / 16.0f, 1.0f / 16.0f, 64).y));
    }

    SECTION("Harmônicos esféricos: céu constante e degradê linear dão a irradiância exata");
    {
        const Sh9 constant = ConvolveIrradiance(ProjectSh9([](const glm::vec3&) { return glm::vec3(0.5f, 1.0f, 2.0f); }, 32));
        CHECK(Near(EvaluateSh9(constant, {0, 1, 0}), glm::vec3(0.5f, 1.0f, 2.0f) * kPi, 1e-2f));
        CHECK(Near(EvaluateSh9(constant, glm::normalize(glm::vec3(1, -2, 3))), glm::vec3(0.5f, 1.0f, 2.0f) * kPi, 1e-2f));
        // L(ω) = 1 + ω.y => E(n) = π + (2π/3)·n.y (só bandas 0 e 1: exato).
        const Sh9 gradient = ConvolveIrradiance(ProjectSh9([](const glm::vec3& d) { return glm::vec3(1.0f + d.y); }, 32));
        CHECK(Near(EvaluateSh9(gradient, {0, 1, 0}).x, kPi + 2.0f * kPi / 3.0f, 1e-2f));
        CHECK(Near(EvaluateSh9(gradient, {0, -1, 0}).x, kPi - 2.0f * kPi / 3.0f, 1e-2f));
        CHECK(Near(EvaluateSh9(gradient, {1, 0, 0}).x, kPi, 1e-2f));
        // Céu claro em cima, escuro embaixo: o chão (normal para cima) recebe mais luz que o teto.
        const Sh9 sky = ConvolveIrradiance(ProjectSh9([](const glm::vec3& d) { return GradientSky(d, glm::vec3(0.3f, 0.5f, 0.8f)); }));
        CHECK(EvaluateSh9(sky, {0, 1, 0}).b > EvaluateSh9(sky, {0, -1, 0}).b);
    }

    SECTION("Céu de cor: horizonte = cor, zênite e chão mais escuros");
    {
        const glm::vec3 color{0.2f, 0.4f, 0.8f};
        CHECK(Near(GradientSky({1, 0, 0}, color), color));
        CHECK(GradientSky({0, 1, 0}, color).b < color.b && GradientSky({0, -1, 0}, color).b < GradientSky({0, 1, 0}, color).b);
        Environment env;
        CHECK(Near(SkyRadiance(env, {0, 0, -1}), SrgbToLinear(env.skyColor)));
        env.skyMode = SkyMode::Texture;  // sem imagem: cai na cor
        CHECK(Near(SkyRadiance(env, {0, 0, -1}), SrgbToLinear(env.skyColor)));
    }

    SECTION("Imagem equirretangular: coordenadas, amostragem e rotação");
    {
        CHECK(Near(EquirectUv({0, 0, -1}).x, 0.5f) && Near(EquirectUv({0, 0, -1}).y, 0.5f));
        CHECK(Near(EquirectUv({0, 1, 0}).y, 0.0f) && Near(EquirectUv({0, -1, 0}).y, 1.0f));
        CHECK(Near(EquirectUv({1, 0, 0}).x, 0.75f) && Near(EquirectUv({-1, 0, 0}).x, 0.25f));
        CHECK(Near(RotateY({0, 0, -1}, glm::radians(90.0f)), glm::vec3(-1, 0, 0)));
        // Metade de cima vermelha, de baixo azul (linha 0 = topo).
        auto image = std::make_shared<HdrImage>();
        image->width = 8;
        image->height = 4;
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 8; ++x) {
                const glm::vec3 c = y < 2 ? glm::vec3(4.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
                image->rgb.insert(image->rgb.end(), {c.r, c.g, c.b});
            }
        }
        CHECK(Near(SampleEquirect(*image, {0, 1, 0}), glm::vec3(4, 0, 0)) && Near(SampleEquirect(*image, {0, -1, 0}), glm::vec3(0, 0, 1)));
        Environment env;
        env.skyMode = SkyMode::Texture;
        env.skyTexture = image;
        CHECK(Near(SkyRadiance(env, {0, 1, 0}), glm::vec3(4, 0, 0)));
    }

    SECTION("Brilho antigo -> rugosidade");
    CHECK(Near(RoughnessFromShininess(4.0f), 1.0f) && Near(RoughnessFromShininess(128.0f), 0.0f));
    CHECK(Near(RoughnessFromShininess(1.0f), 1.0f) && Near(RoughnessFromShininess(1000.0f), 0.0f));
    CHECK(RoughnessFromShininess(16.0f) > RoughnessFromShininess(64.0f));

    SECTION("Imagem HDR (.hdr) lida pelo stb_image e como asset do projeto");
    const fs::path project = root / PathFromUtf8("Jogo Ação");
    const fs::path skyFile = project / "Assets" / PathFromUtf8("Céus") / "tarde.hdr";
    fs::create_directories(skyFile.parent_path());
    // RGBE: (128, 128, 128, 129) = 1.0; expoente 130 = 2.0.
    WriteHdr(skyFile, 2, 1, {{128, 128, 128, 129}, {128, 64, 0, 130}});
    {
        HdrImage hdr;
        CHECK(LoadHdrFile(skyFile, hdr, &error));
        CHECK(hdr.width == 2 && hdr.height == 1 && hdr.rgb.size() == 6);
        CHECK(hdr.rgb.size() == 6 && Near(hdr.Pixel(0, 0), glm::vec3(1.0f)) && Near(hdr.Pixel(1, 0), glm::vec3(2.0f, 1.0f, 0.0f)));
        CHECK(!DecodeHdrImage(std::vector<uint8_t>{1, 2, 3}, hdr, &error));
        CHECK(AssetTypeFromPath(skyFile) == AssetType::EnvironmentMap && std::string(AssetTypeName(AssetType::EnvironmentMap)) == "Céu HDR");
    }
    AssetLibrary assets;
    assets.SetRoot(project);
    assets.Database().Refresh();
    const std::string skyKey = assets.KeyForFile(skyFile);
    const auto skyImage = assets.GetEnvironmentMap(skyKey, &error);
    CHECK(skyImage && skyImage->width == 2);
    CHECK(assets.GetEnvironmentMap(skyKey) == skyImage);  // cache
    CHECK(assets.RefOf(skyImage.get()) && assets.RefOf(skyImage.get())->name == skyKey);
    CHECK(!assets.GetEnvironmentMap("Assets/nao_existe.hdr", &error));

    SECTION("Cena versão 6: ambiente completo (céu, sombras, pós) vai e volta igual");
    {
        Scene scene;
        Environment& env = scene.environment;
        env.skyMode = SkyMode::Texture;
        env.skyTexture = skyImage;
        env.skyIntensity = 1.5f;
        env.skyRotation = 30.0f;
        env.skyLight = 0.8f;
        env.shadowDistance = 80.0f;
        env.exposure = 1.25f;
        env.toneMapper = ToneMapper::Reinhard;
        env.bloom = false;
        env.bloomIntensity = 0.7f;
        env.bloomThreshold = 2.0f;
        env.fxaa = false;
        Entity& lamp = scene.CreateEntity("Lâmpada");
        LightComponent& light = lamp.Add<LightComponent>();
        light.castShadows = false;
        light.shadowQuality = ShadowQuality::High;
        Entity& box = scene.CreateEntity("Caixa");
        MeshRenderer& mr = box.Add<MeshRenderer>();
        mr.material.metallic = 1.0f;
        mr.material.roughness = 0.15f;
        mr.material.emissive = {1.0f, 0.5f, 0.0f};
        mr.material.emissiveIntensity = 4.0f;

        const std::string text = SceneSerializer::ToJson(scene, assets);
        CHECK(text.find("\"version\": 6") != std::string::npos);
        CHECK(text.find("\"toneMapper\": \"Reinhard\"") != std::string::npos);
        CHECK(text.find("\"shadowQuality\": \"High\"") != std::string::npos);
        CHECK(text.find("\"skyTexture\"") != std::string::npos && text.find("tarde.hdr") != std::string::npos);
        Scene back;
        CHECK(SceneSerializer::FromJson(text, back, assets, &error));
        CHECK(back.environment == scene.environment);
        CHECK(back == scene);
        const LightComponent* backLight = back.FindByName("Lâmpada") ? back.FindByName("Lâmpada")->Get<LightComponent>() : nullptr;
        CHECK(backLight && !backLight->castShadows && backLight->shadowQuality == ShadowQuality::High);
        // Luz no renderer leva a sombra junto.
        const std::vector<Light> lights = back.CollectLights();
        CHECK(lights.size() == 1 && !lights[0].castShadows && lights[0].shadowQuality == ShadowQuality::High);

        // Mudança no ambiente entra no desfazer (Scene::operator==).
        Scene changed = scene;
        changed.environment.bloomThreshold = 1.5f;
        CHECK(!(changed == scene));
    }

    SECTION("Cena antiga (versão 5): padrões da Fase 7 e brilho virando rugosidade");
    {
        const char* v5 = R"({"format": "motor-scene", "version": 5,
            "environment": {"skyColor": [0.2, 0.3, 0.4], "fogDensity": 0.01, "ambient": [0.1, 0.1, 0.1]},
            "entities": [
              {"id": 1, "name": "Sol", "components": [{"type": "Light", "kind": "Directional"}]},
              {"id": 2, "name": "Bola", "components": [{"type": "MeshRenderer", "mesh": null, "specular": 0.5, "shininess": 64}]}
            ]})";
        Scene old;
        CHECK(SceneSerializer::FromJson(v5, old, assets, &error));
        CHECK(old.environment.skyColor == glm::vec3(0.2f, 0.3f, 0.4f));
        CHECK(old.environment.skyMode == SkyMode::Color && !old.environment.skyTexture);
        CHECK(old.environment.toneMapper == ToneMapper::Aces && old.environment.bloom && old.environment.fxaa);
        const LightComponent* sunLight = old.FindByName("Sol") ? old.FindByName("Sol")->Get<LightComponent>() : nullptr;
        CHECK(sunLight && sunLight->castShadows && sunLight->shadowQuality == ShadowQuality::Medium);
        const MeshRenderer* ball = old.FindByName("Bola") ? old.FindByName("Bola")->Get<MeshRenderer>() : nullptr;
        CHECK(ball && Near(ball->material.roughness, RoughnessFromShininess(64.0f)));
        CHECK(ball && ball->material.metallic == 0.0f);
        // Valores estranhos no arquivo: ignorados (leitura tolerante).
        Scene odd;
        CHECK(SceneSerializer::FromJson(R"({"format": "motor-scene", "version": 6,
            "environment": {"sky": "Nuvem", "toneMapper": 3, "bloom": "sim", "exposure": "alta"}, "entities": []})", odd, assets, &error));
        CHECK(odd.environment == Environment{});
    }

    SECTION("Material .mmat versão 2 com mapas, e a 1 convertida");
    {
        Material m;
        m.metallic = 0.5f;
        m.roughness = 0.35f;
        m.occlusionStrength = 0.6f;
        m.normalStrength = 1.5f;
        const std::string text = assets.MaterialToJson(m);
        CHECK(text.find("\"version\": 3") != std::string::npos && text.find("\"roughness\": 0.35") != std::string::npos);
        CHECK(text.find("\"normalMap\": null") != std::string::npos && text.find("\"shininess\"") == std::string::npos);
        Material back;
        CHECK(assets.MaterialFromJson(text, back, &error));
        CHECK(back.metallic == 0.5f && back.roughness == 0.35f && back.occlusionStrength == 0.6f && back.normalStrength == 1.5f);
        Material v1;
        CHECK(assets.MaterialFromJson(R"({"format": "motor-material", "version": 1, "specular": 0.3, "shininess": 128})", v1, &error));
        CHECK(Near(v1.roughness, 0.0f) && v1.specular == 0.3f && v1.metallic == 0.0f);
    }

    SECTION("MaterialParams: o que o lit.frag recebe e quando o material muda (Fase 23)");
    {
        Material m;
        m.blend = BlendMode::Cutout;
        m.alphaCutoff = 1.7f;
        m.emissive = glm::vec3(0.5f);
        m.emissiveIntensity = -2.0f;
        const MaterialParams p = MaterialParamsOf(m);
        CHECK(p.alphaMode == glm::vec4(static_cast<float>(BlendMode::Cutout), 1.0f, 0.0f, 0.0f));  // recorte limitado
        CHECK(p.emissive == glm::vec4(0.0f));  // intensidade negativa = sem luz própria
        CHECK(p.pbr.w == 0.0f && p.albedo == nullptr);  // sem mapa de normal, sem textura

        m.emissiveIntensity = 2.0f;
        CHECK(Near(glm::vec3(MaterialParamsOf(m).emissive), SrgbToLinear(glm::vec3(0.5f)) * 2.0f));
        m.emissive = glm::vec3(0.0f);
        m.emissiveIntensity = 5.0f;
        CHECK(MaterialParamsOf(m).emissive == glm::vec4(0.0f));  // preto não brilha, com qualquer intensidade

        // O renderer só reenvia quando o resultado muda. As texturas são comparadas pelo ponteiro; sem GPU no teste,
        // um endereço qualquer serve (nunca é lido).
        Material a, b;
        CHECK(MaterialParamsOf(a) == MaterialParamsOf(b));
        b.color.r = 0.9f;
        CHECK(!(MaterialParamsOf(a) == MaterialParamsOf(b)));
        b = a;
        int dummy = 0;
        b.albedo = std::shared_ptr<Texture2D>(reinterpret_cast<Texture2D*>(&dummy), [](Texture2D*) {});
        CHECK(!(MaterialParamsOf(a) == MaterialParamsOf(b)) && MaterialParamsOf(b).albedo == b.albedo.get());
        a.albedo = b.albedo;
        CHECK(MaterialParamsOf(a) == MaterialParamsOf(b));
        b.occlusionStrength = 0.3f;  // sem mapa de oclusão, a força não chega ao shader
        CHECK(MaterialParamsOf(a) == MaterialParamsOf(b));
    }

    SECTION("Matriz da normal pela cofatora: a mesma direção da inversa transposta (Fase 23)");
    {
        // Rotação, escala não uniforme e translação; depois um espelho (escala negativa).
        glm::mat4 m(1.0f);
        m = glm::translate(m, glm::vec3(3.0f, -1.0f, 2.0f));
        m = glm::rotate(m, 0.7f, glm::normalize(glm::vec3(1.0f, 2.0f, 0.5f)));
        m = glm::scale(m, glm::vec3(2.0f, 0.5f, 3.0f));
        for (int k = 0; k < 2; ++k) {
            const glm::mat3 reference = glm::inverseTranspose(glm::mat3(m));
            for (const glm::vec3& n : {glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::normalize(glm::vec3(1, -2, 3))}) {
                CHECK(Near(glm::normalize(NormalMatrix(m) * n), glm::normalize(reference * n)));
            }
            m = glm::scale(m, glm::vec3(-1.0f, 1.0f, 1.0f));
        }
    }

    SECTION("Ordem de desenho: números de material e lotes seguidos (Fase 23)");
    {
        // Materiais de objetos diferentes com os mesmos valores ganham o mesmo número; duas faces separa.
        Material azul, azul2, vermelho, azulDuasFaces;
        azul.color = azul2.color = azulDuasFaces.color = glm::vec4(0.2f, 0.4f, 1.0f, 1.0f);
        vermelho.color = glm::vec4(1.0f, 0.1f, 0.1f, 1.0f);
        azulDuasFaces.doubleSided = true;
        MaterialIds ids;
        auto id = [&](const Material& m) { return ids.Id({MaterialParamsOf(m), m.doubleSided}); };
        CHECK(id(azul) == 0 && id(vermelho) == 1 && id(azul2) == 0 && id(azulDuasFaces) == 2);
        CHECK(ids.Size() == 3 && ids[1].params.color == vermelho.color);
        ids.Clear();
        CHECK(ids.Size() == 0 && id(vermelho) == 0);

        // Sombra: só recorte e alfa contam (a cor não separa os lotes).
        ShadowParamsIds shadowIds;
        CHECK(shadowIds.Id(ShadowParamsOf(azul)) == shadowIds.Id(ShadowParamsOf(vermelho)));
        Material grade;
        grade.blend = BlendMode::Cutout;
        grade.alphaCutoff = 0.5f;
        grade.color.a = 0.3f;  // recorte sem textura e alfa baixo: some inteiro
        CHECK(ShadowParamsOf(grade).cutoff == 2.0f && ShadowParamsOf(azul).cutoff == -1.0f);

        // Intercalados (cubo, esfera, cubo...) com dois materiais: a ordem junta os iguais, sem trocar a ordem
        // de quem tem a mesma chave; com pele (shader 1) vai para o fim.
        int cubo = 0, esfera = 0;
        const std::vector<DrawKey> keys = {
            {0, 1, &cubo}, {0, 0, &esfera}, {1, 0, &cubo}, {0, 1, &cubo}, {0, 0, &esfera}, {0, 0, &cubo},
        };
        std::vector<uint32_t> order;
        SortByKey(keys, order);
        CHECK(order.size() == keys.size());
        std::vector<int> runs;  // tamanho de cada sequência de chaves iguais
        for (size_t k = 0; k < order.size(); ++k) {
            if (k == 0 || !(keys[order[k]] == keys[order[k - 1]])) runs.push_back(0);
            ++runs.back();
        }
        CHECK(runs.size() == 4);  // (0,0,esfera)x2, (0,0,cubo), (0,1,cubo)x2, (1,0,cubo)
        CHECK(keys[order.back()].shader == 1);
        // Mesma chave: na ordem original.
        for (size_t k = 1; k < order.size(); ++k) {
            if (keys[order[k]] == keys[order[k - 1]]) CHECK(order[k] > order[k - 1]);
        }
    }

    SECTION("Renderização relativa à câmera: longe da origem não perde precisão (Fase 28)");
    {
        // Câmera a 10 km da origem, olhando um ponto a 3 m. A conta no mundo em float (o que a GPU fazia) erra o
        // lugar na tela; relativa à câmera, fica igual à conta em double.
        Camera camera;
        camera.position = {10000.37f, 2.0f, -10000.71f};
        camera.LookAt(camera.position + glm::vec3(0.3f, -0.1f, -1.0f));
        const glm::mat4 projection = camera.Projection();
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), camera.position + glm::vec3(0.6f, -0.2f, -3.0f));
        const glm::vec4 local(0.013f, 0.021f, -0.017f, 1.0f);

        const glm::dmat4 exactModel = glm::translate(glm::dmat4(1.0), glm::dvec3(model[3]));
        const glm::dmat4 exactView = glm::lookAt(glm::dvec3(camera.position),
                                                 glm::dvec3(camera.position) + glm::dvec3(camera.Forward()),
                                                 glm::dvec3(camera.Up()));
        const glm::dvec4 exactClip = glm::dmat4(projection) * exactView * exactModel * glm::dvec4(local);
        const glm::dvec2 exact = glm::dvec2(exactClip) / exactClip.w;

        const glm::vec4 relativeClip = projection * camera.ViewRotation() *
                                       (ModelRelativeToOrigin(model, camera.position) * local);
        const glm::vec2 relative = glm::vec2(relativeClip) / relativeClip.w;
        const glm::vec4 worldClip = projection * camera.View() * (model * local);
        const glm::vec2 inWorldFloat = glm::vec2(worldClip) / worldClip.w;
        std::printf("  erro na tela: relativo %.2e, no mundo %.2e\n", glm::length(glm::dvec2(relative) - exact),
                    glm::length(glm::dvec2(inWorldFloat) - exact));
        CHECK(glm::length(glm::dvec2(relative) - exact) < 1e-7);
        CHECK(glm::length(glm::dvec2(relative) - exact) * 50.0 < glm::length(glm::dvec2(inWorldFloat) - exact));

        // Luz e sombra: a matriz do mundo aplicada ao ponto relativo dá o mesmo que no mundo.
        const glm::mat4 lightViewProjection = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 0.1f, 100.0f) *
                                              glm::lookAt(camera.position + glm::vec3(10, 30, 5), camera.position,
                                                          glm::vec3(0, 1, 0));
        const glm::vec3 world = camera.position + glm::vec3(1.5f, -1.0f, 2.5f);
        const glm::vec4 inWorld = lightViewProjection * glm::vec4(world, 1.0f);
        const glm::vec4 inRelative =
            RelativeToOrigin(lightViewProjection, camera.position) * glm::vec4(world - camera.position, 1.0f);
        CHECK(glm::length(inWorld - inRelative) < 1e-3f);
        const glm::mat4 rotation = camera.ViewRotation();
        CHECK(rotation[3] == glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    }

    return TestResult();
}
