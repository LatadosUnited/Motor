#include "renderer/Shadows.h"

#include "motor/core/JobSystem.h"
#include "motor/core/FrameStats.h"
#include "motor/core/Profile.h"
#include "motor/renderer/Renderer.h"
#include "motor/renderer/GpuMemory.h"
#include "renderer/GL.h"
#include "renderer/GpuProfile.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>

namespace motor {

namespace {
constexpr float kCascadeLambda = 0.8f;  // mais perto da divisão logarítmica: mais detalhe perto da câmera
constexpr float kPointNear = 0.05f;

// Profundidade das cascatas em 16 bits: cada uma cobre no máximo algumas centenas de metros em projeção ortogonal
// (passos de poucos milímetros, abaixo do deslocamento contra o "acne") e grava e lê metade do 32F. Os cubos das
// pontuais guardam a distância até a luz (até o alcance), em 24 bits.
constexpr GLenum kCascadeFormat = GL_DEPTH_COMPONENT16;
constexpr GLenum kCubeFormat = GL_DEPTH_COMPONENT24;

GLuint CreateShadowTexture(GLenum target, int resolution, int layers) {
    const GLenum format = target == GL_TEXTURE_2D_ARRAY ? kCascadeFormat : kCubeFormat;
    GLuint texture = 0;
    glCreateTextures(target, 1, &texture);
    glTextureStorage3D(texture, 1, format, resolution, resolution, layers);
    TrackGpuMemory(GpuObject::Texture, texture, GpuMemoryKind::Shadows, GpuTextureBytes(format, resolution, resolution, layers));
    // Comparação no hardware (sampler...Shadow): com LINEAR, cada leitura já é um PCF 2x2.
    glTextureParameteri(texture, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(texture, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (target == GL_TEXTURE_2D_ARRAY) {
        // Fora do mapa = sem sombra.
        const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glTextureParameterfv(texture, GL_TEXTURE_BORDER_COLOR, border);
    } else {
        glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(texture, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
    return texture;
}
} // namespace

ShadowRenderer::Variant ShadowRenderer::MakeVariant(const std::filesystem::path& dir, std::string_view defines) {
    Variant v;
    v.shader = Shader::FromFiles(dir / "shadow.vert", dir / "shadow.frag", defines);
    Shader& s = *v.shader;
    const bool point = defines.find("POINT") != std::string_view::npos;
    v.uniforms = {s.Uniform("uLightViewProjection"), point ? s.Uniform("uLightPositionRange") : -1,
                  s.Uniform("uAlphaCutoff"), s.Uniform("uColorAlpha")};
    return v;
}

ShadowRenderer::ShadowRenderer(const std::filesystem::path& dir)
    : m_directional(MakeVariant(dir, "")),
      m_directionalSkinned(MakeVariant(dir, "#define SKINNED\n")),
      m_point(MakeVariant(dir, "#define POINT\n")),
      m_pointSkinned(MakeVariant(dir, "#define POINT\n#define SKINNED\n")) {
    glCreateFramebuffers(1, &m_fbo);
    glNamedFramebufferDrawBuffer(m_fbo, GL_NONE);
    glNamedFramebufferReadBuffer(m_fbo, GL_NONE);
    // Texturas mínimas: os samplers de sombra do lit.frag sempre têm algo válido vinculado.
    EnsureCascades(1);
    EnsureCubes(1);
}

ShadowRenderer::~ShadowRenderer() {
    glDeleteFramebuffers(1, &m_fbo);
    UntrackGpuMemory(GpuObject::Texture, m_cascades);
    UntrackGpuMemory(GpuObject::Texture, m_cubes);
    glDeleteTextures(1, &m_cascades);
    glDeleteTextures(1, &m_cubes);
}

void ShadowRenderer::EnsureCascades(int resolution) {
    if (resolution == m_cascadeResolution) return;
    UntrackGpuMemory(GpuObject::Texture, m_cascades);
    if (m_cascades) glDeleteTextures(1, &m_cascades);
    m_cascades = CreateShadowTexture(GL_TEXTURE_2D_ARRAY, resolution, kCascades);
    m_cascadeResolution = resolution;
    m_drawnCount = 0;  // mapa novo, vazio
}

void ShadowRenderer::EnsureCubes(int resolution) {
    if (resolution == m_cubeResolution) return;
    UntrackGpuMemory(GpuObject::Texture, m_cubes);
    if (m_cubes) glDeleteTextures(1, &m_cubes);
    m_cubes = CreateShadowTexture(GL_TEXTURE_CUBE_MAP_ARRAY, resolution, static_cast<int>(6 * kMaxPointShadows));
    m_cubeResolution = resolution;
}

void ShadowRenderer::BuildPasses(std::span<const DrawItem> items, const ShadowOrder& order) {
    MOTOR_PROFILE_STAT("Render: listas das sombras");
    JobSystem& jobs = JobSystem::Get();
    // 1) Recorte: um bit por passada em cada item, os itens divididos entre as threads.
    m_masks.resize(items.size());
    jobs.ParallelFor(items.size(), 512, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            const DrawItem& item = items[i];
            uint32_t mask = 0;
            // Vidro e água não fazem sombra. Do RenderWorld, as passadas já saíram da grade (Fase 28).
            if (!item.transparent && item.shadows != ShadowCasting::Off) {
                if (item.retained) {
                    mask = item.shadowMask;
                } else {
                    for (size_t p = 0; p < m_passCount; ++p) {
                        if (!m_passes[p].skip && m_passes[p].frustum.Intersects(item.bounds)) mask |= 1u << p;
                    }
                }
            }
            m_masks[i] = mask;
        }
    });
    // 2) Lotes de cada passada, as passadas divididas entre as threads. Na ordem das sombras, itens seguidos com a
    //    mesma chave (shader, recorte/alfa, malha) viram um draw instanciado; com pele, um por item (as juntas são dele).
    jobs.ParallelFor(m_passCount, 1, [&](size_t begin, size_t end) {
        for (size_t p = begin; p < end; ++p) {
            Pass& pass = m_passes[p];
            const uint32_t bit = 1u << p;
            pass.indices.clear();
            pass.batches.clear();
            for (const uint32_t i : order.order) {
                if (!(m_masks[i] & bit)) continue;
                const DrawItem& item = items[i];
                if (!pass.batches.empty() && !item.skinned && order.keys[i] == order.keys[pass.batches.back().firstIndex]) {
                    ++pass.batches.back().count;
                    pass.indices.push_back(i);
                    continue;
                }
                pass.batches.push_back({&item, static_cast<uint32_t>(pass.indices.size()), 1, i});
                pass.indices.push_back(i);
            }
        }
    });
}

void ShadowRenderer::DrawPass(const Pass& pass, const glm::vec3& origin, InstanceBuffers& instances,
                              const ShadowOrder& order, const JointUploader& uploadJoints, RenderStats& stats) {
    const float clearDepth = 1.0f;
    glViewport(0, 0, pass.resolution, pass.resolution);
    glNamedFramebufferTextureLayer(m_fbo, GL_DEPTH_ATTACHMENT, pass.texture, 0, pass.layer);
    glClearNamedFramebufferfv(m_fbo, GL_DEPTH, 0, &clearDepth);
    if (pass.skip) return;
    ++stats.shadowPasses;
    if (pass.batches.empty()) return;
    instances.UploadPass(pass.indices, pass.batches);

    Variant& base = pass.point ? m_point : m_directional;
    Variant& skinned = pass.point ? m_pointSkinned : m_directionalSkinned;
    for (Variant* v : {&base, &skinned}) {
        // As passadas ficam no mundo (recorte e cache das cascatas); o buffer de objetos é relativo à câmera.
        v->shader->SetMat4(v->uniforms.lightViewProjection, RelativeToOrigin(pass.viewProjection, origin));
        if (pass.point) {
            v->shader->SetVec4(v->uniforms.lightPositionRange, pass.positionRange - glm::vec4(origin, 0.0f));
        }
    }
    const Variant* bound = nullptr;
    // Recorte e alfa do lote anterior (por shader): o mesmo valor seguido não reenvia nada.
    const Variant* paramsVariant = nullptr;
    const ShadowParams* lastParams = nullptr;
    GLuint lastAlbedo = 0;
    // Lotes seguidos com o mesmo recorte e alfa, sem pele e na mesma malha-VAO (todas sem pele dividem um) saem num
    // draw indireto só (Fase 28): numa passada sem recorte, em geral um draw para tudo.
    const std::vector<DrawBatch>& batches = pass.batches;
    auto vao = [](const DrawBatch& b) { return b.first->mesh->Gpu().vao; };
    for (size_t i = 0; i < batches.size();) {
        const DrawBatch& batch = batches[i];
        const DrawItem& item = *batch.first;
        const uint32_t paramsId = order.keys[batch.firstIndex].material;
        size_t end = i + 1;
        if (!item.skinned) {
            while (end < batches.size() && !batches[end].first->skinned &&
                   order.keys[batches[end].firstIndex].material == paramsId && vao(batches[end]) == vao(batch)) {
                ++end;
            }
        }
        const ShadowParams& params = (*order.params)[paramsId];
        Variant& variant = item.skinned ? skinned : base;
        Shader& shader = *variant.shader;
        if (bound != &variant) {
            shader.Bind();
            bound = &variant;
        }
        if (item.skinned) uploadJoints(item);
        if (paramsVariant != &variant || !lastParams || !(params == *lastParams)) {
            shader.SetFloat(variant.uniforms.alphaCutoff, params.cutoff);
            shader.SetFloat(variant.uniforms.colorAlpha, params.alpha);
            paramsVariant = &variant;
            lastParams = &params;
        }
        if (params.albedo && params.albedo->NativeHandle() != lastAlbedo) {
            lastAlbedo = params.albedo->NativeHandle();
            params.albedo->Bind(0);
        }
        instances.Draw(i, end - i);
        ++stats.drawCalls;
        ++stats.shadowDrawCalls;
        i = end;
    }
}

void ShadowRenderer::Plan(const Camera& camera, std::span<const Light> lights, const Environment& env) {
    m_planned = Result{};
    m_planned.cubeLayer.assign(lights.size(), -1);
    const float distance = std::clamp(env.shadowDistance, 1.0f, std::max(camera.farPlane, 1.0f));
    // Primeiro as passadas do quadro (cascatas e faces de cubo); no Render, as listas de todas em paralelo e o
    // desenho. Até 4 cascatas e 6 faces por luz pontual: um bit de m_masks (e do shadowMask dos itens) por passada.
    static_assert(kCascades + 6 * kMaxPointShadows <= 32);
    m_passes.resize(kCascades + 6 * kMaxPointShadows);
    m_passCount = 0;
    m_cascadePasses = AddCascadePasses(camera, lights, distance, m_planned);
    AddCubePasses(camera, lights, distance, m_planned);
}

ShadowRenderer::Result ShadowRenderer::Render(const glm::vec3& origin, std::span<const DrawItem> items,
                                              InstanceBuffers& instances, const ShadowOrder& order,
                                              const JointUploader& uploadJoints, RenderStats& stats) {
    Result result = std::move(m_planned);
    const size_t cascadePasses = m_cascadePasses;
    if (m_passCount > 0) BuildPasses(items, order);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);  // planos de um lado só também projetam sombra
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    // Desvio de profundidade só nas cascatas (as primeiras passadas).
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
    for (size_t p = 0; p < m_passCount; ++p) {
        if (p == cascadePasses) glDisable(GL_POLYGON_OFFSET_FILL);
        DrawPass(m_passes[p], origin, instances, order, uploadJoints, stats);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glNamedFramebufferTexture(m_fbo, GL_DEPTH_ATTACHMENT, 0, 0);
    // O lit.frag lê as cascatas com a posição relativa à câmera.
    for (glm::mat4& matrix : result.data.cascadeMatrices) matrix = RelativeToOrigin(matrix, origin);
    return result;
}

// Direcional: a primeira com sombra, em cascatas. Devolve quantas passadas são de cascata (as primeiras).
size_t ShadowRenderer::AddCascadePasses(const Camera& camera, std::span<const Light> lights, float distance,
                                        Result& result) {
    for (size_t i = 0; i < lights.size(); ++i) {
        const Light& light = lights[i];
        if (light.type != LightType::Directional || !light.castShadows) continue;
        MOTOR_PROFILE_GPU("Sombras cascatas");
        const int resolution = ShadowResolution(LightType::Directional, light.shadowQuality);
        EnsureCascades(resolution);
        // Qualidade baixa: 2 cascatas (metade das passadas); as outras, 4. O filtro (PCF, mistura nas divisas) é variante do lit.frag.
        const int cascades = light.shadowQuality == ShadowQuality::Low ? 2 : kCascades;
        const std::vector<float> splits = CascadeSplits(camera.nearPlane, distance, cascades, kCascadeLambda);
        // As duas cascatas mais longe (com 4) são desenhadas 15% maiores e redesenhadas em quadros alternados: no
        // outro quadro fica o mapa já desenhado, se ainda cobrir o pedaço da câmera (CascadeCovers). Objetos que se
        // mexem atrasam um quadro lá longe. Outra câmera (editor) ou luz diferente: tudo desenhado de novo.
        const bool sameSetup = m_drawnCount == cascades && m_drawnDirection == light.direction;
        ++m_frame;
        for (int c = 0; c < cascades; ++c) {
            const float nearDist = c == 0 ? camera.nearPlane : splits[static_cast<size_t>(c - 1)];
            const float farDist = splits[static_cast<size_t>(c)];
            const float casterDepth = std::max(50.0f, distance);
            const bool distant = cascades > 2 && c >= cascades - 2;
            Cascade cascade = ComputeCascade(camera, nearDist, farDist, light.direction, resolution, casterDepth);
            const Cascade& drawn = m_drawnCascades[c];
            if (distant && sameSetup && (m_frame + static_cast<uint64_t>(c)) % 2 != 0 && CascadeCovers(drawn, cascade)) {
                result.data.cascadeMatrices[c] = drawn.viewProjection;
                result.data.cascadeSplits[c] = farDist;
                result.data.cascadeTexels[c] = drawn.texelSize;
                continue;
            }
            if (distant) cascade = ComputeCascade(camera, nearDist, farDist, light.direction, resolution, casterDepth, 1.15f);
            m_drawnCascades[c] = cascade;
            Pass& pass = m_passes[m_passCount++];
            pass.viewProjection = cascade.viewProjection;
            pass.frustum = Frustum::FromMatrix(cascade.viewProjection);
            pass.point = false;
            pass.skip = false;
            pass.texture = m_cascades;
            pass.layer = c;
            pass.resolution = resolution;
            result.data.cascadeMatrices[c] = cascade.viewProjection;
            result.data.cascadeSplits[c] = farDist;
            result.data.cascadeTexels[c] = cascade.texelSize;
        }
        result.data.params = glm::vec4(static_cast<float>(cascades), 1.0f / static_cast<float>(resolution), 0.0f, distance);
        result.cascadeQuality = light.shadowQuality;
        m_drawnCount = cascades;
        m_drawnDirection = light.direction;
        result.cascadeLight = static_cast<int>(i);
        return m_passCount;
    }
    return 0;
}

// Pontuais e spots: as mais perto da câmera que iluminam algo visível, seis faces de cubo cada.
void ShadowRenderer::AddCubePasses(const Camera& camera, std::span<const Light> lights, float distance,
                                   Result& result) {
    const Frustum view = Frustum::FromMatrix(camera.Projection() * camera.View());
    struct Candidate {
        size_t light;
        float distance;
    };
    std::vector<Candidate> candidates;
    for (size_t i = 0; i < lights.size(); ++i) {
        const Light& light = lights[i];
        if (light.type == LightType::Directional || !light.castShadows || light.range <= 0.0f) continue;
        const float d = glm::length(light.position - camera.position);
        if (d - light.range > distance || !view.IntersectsSphere(light.position, light.range)) continue;
        candidates.push_back({i, d});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
    if (candidates.size() > kMaxPointShadows) candidates.resize(kMaxPointShadows);

    if (!candidates.empty()) {
        MOTOR_PROFILE_GPU("Sombras cubos");
        int resolution = 1;
        for (const Candidate& c : candidates) {
            resolution = std::max(resolution, ShadowResolution(lights[c.light].type, lights[c.light].shadowQuality));
        }
        EnsureCubes(resolution);
        for (size_t slot = 0; slot < candidates.size(); ++slot) {
            const Light& light = lights[candidates[slot].light];
            const glm::mat4 projection = CubeFaceProjection(kPointNear, light.range);
            for (int face = 0; face < 6; ++face) {
                Pass& pass = m_passes[m_passCount++];
                pass.viewProjection = projection * CubeFaceView(light.position, face);
                pass.frustum = Frustum::FromMatrix(pass.viewProjection);
                pass.point = true;
                // Spot: só as faces que o cone alcança são desenhadas (as outras ficam só limpas, sem sombra).
                pass.skip = light.type == LightType::Spot && !SpotCoversCubeFace(light.direction, light.outerAngle, face);
                pass.positionRange = glm::vec4(light.position, light.range);
                pass.texture = m_cubes;
                pass.layer = static_cast<int>(slot) * 6 + face;
                pass.resolution = resolution;
            }
            result.cubeLayer[candidates[slot].light] = static_cast<int>(slot);
        }
        result.data.params.z = 1.0f / static_cast<float>(resolution);
    }
}

} // namespace motor
