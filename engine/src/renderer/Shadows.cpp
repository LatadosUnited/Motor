#include "renderer/Shadows.h"

#include "motor/renderer/Renderer.h"
#include "motor/renderer/GpuMemory.h"
#include "renderer/GL.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>

namespace motor {

namespace {
constexpr float kCascadeLambda = 0.8f;  // mais perto da divisão logarítmica: mais detalhe perto da câmera
constexpr float kPointNear = 0.05f;

GLuint CreateShadowTexture(GLenum target, int resolution, int layers) {
    GLuint texture = 0;
    glCreateTextures(target, 1, &texture);
    glTextureStorage3D(texture, 1, GL_DEPTH_COMPONENT32F, resolution, resolution, layers);
    TrackGpuMemory(GpuObject::Texture, texture, GpuMemoryKind::Shadows,
                   GpuTextureBytes(GL_DEPTH_COMPONENT32F, resolution, resolution, layers));
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
                  s.Uniform("uModel"), s.Uniform("uAlphaCutoff"), s.Uniform("uColorAlpha")};
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
}

void ShadowRenderer::EnsureCubes(int resolution) {
    if (resolution == m_cubeResolution) return;
    UntrackGpuMemory(GpuObject::Texture, m_cubes);
    if (m_cubes) glDeleteTextures(1, &m_cubes);
    m_cubes = CreateShadowTexture(GL_TEXTURE_CUBE_MAP_ARRAY, resolution, static_cast<int>(6 * kMaxPointShadows));
    m_cubeResolution = resolution;
}

void ShadowRenderer::DrawItems(std::span<const DrawItem> items, const glm::mat4& viewProjection, const Frustum& frustum,
                               bool point, const glm::vec4& lightPositionRange, const JointUploader& uploadJoints,
                               RenderStats& stats) {
    Variant& base = point ? m_point : m_directional;
    Variant& skinned = point ? m_pointSkinned : m_directionalSkinned;
    for (Variant* v : {&base, &skinned}) {
        v->shader->SetMat4(v->uniforms.lightViewProjection, viewProjection);
        if (point) v->shader->SetVec4(v->uniforms.lightPositionRange, lightPositionRange);
    }
    const Variant* bound = nullptr;
    // Recorte e alfa do desenho anterior (por shader): o mesmo material seguido não reenvia nada.
    const Variant* paramsVariant = nullptr;
    float lastCutoff = 0.0f, lastAlpha = 0.0f;
    GLuint lastAlbedo = 0;
    for (const DrawItem& item : items) {
        if (item.material->blend == BlendMode::Transparent) continue;  // vidro e água não fazem sombra
        if (item.shadows == ShadowCasting::Off) continue;
        if (!frustum.Intersects(item.bounds)) continue;
        Variant& variant = item.skinned ? skinned : base;
        Shader& shader = *variant.shader;
        if (bound != &variant) {
            shader.Bind();
            bound = &variant;
        }
        if (item.skinned) uploadJoints(item);
        shader.SetMat4(variant.uniforms.model, item.model);
        const Material& m = *item.material;
        const bool cutout = m.blend == BlendMode::Cutout;
        float cutoff = cutout ? m.alphaCutoff : -1.0f;
        if (cutout && !m.albedo) cutoff = m.color.a < m.alphaCutoff ? 2.0f : -1.0f;  // sem textura
        if (paramsVariant != &variant || cutoff != lastCutoff || m.color.a != lastAlpha) {
            shader.SetFloat(variant.uniforms.alphaCutoff, cutoff);
            shader.SetFloat(variant.uniforms.colorAlpha, m.color.a);
            paramsVariant = &variant;
            lastCutoff = cutoff;
            lastAlpha = m.color.a;
        }
        if (cutout && m.albedo && m.albedo->NativeHandle() != lastAlbedo) {
            lastAlbedo = m.albedo->NativeHandle();
            m.albedo->Bind(0);
        }
        item.mesh->Draw();
        ++stats.drawCalls;
        ++stats.shadowDrawCalls;
    }
}

ShadowRenderer::Result ShadowRenderer::Render(const Camera& camera, std::span<const Light> lights, const Environment& env,
                                              std::span<const DrawItem> items, const JointUploader& uploadJoints,
                                              RenderStats& stats) {
    Result result;
    result.cubeLayer.assign(lights.size(), -1);
    const float distance = std::clamp(env.shadowDistance, 1.0f, std::max(camera.farPlane, 1.0f));

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);  // planos de um lado só também projetam sombra
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    const float clearDepth = 1.0f;

    // --- Direcional: a primeira com sombra, em cascatas ---
    for (size_t i = 0; i < lights.size(); ++i) {
        const Light& light = lights[i];
        if (light.type != LightType::Directional || !light.castShadows) continue;
        const int resolution = ShadowResolution(LightType::Directional, light.shadowQuality);
        EnsureCascades(resolution);
        const std::vector<float> splits = CascadeSplits(camera.nearPlane, distance, kCascades, kCascadeLambda);
        glViewport(0, 0, resolution, resolution);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 2.0f);
        for (int c = 0; c < kCascades; ++c) {
            const float nearDist = c == 0 ? camera.nearPlane : splits[static_cast<size_t>(c - 1)];
            const Cascade cascade = ComputeCascade(camera, nearDist, splits[static_cast<size_t>(c)], light.direction,
                                                   resolution, std::max(50.0f, distance));
            glNamedFramebufferTextureLayer(m_fbo, GL_DEPTH_ATTACHMENT, m_cascades, 0, c);
            glClearNamedFramebufferfv(m_fbo, GL_DEPTH, 0, &clearDepth);
            DrawItems(items, cascade.viewProjection, Frustum::FromMatrix(cascade.viewProjection), false, glm::vec4(0.0f),
                      uploadJoints, stats);
            ++stats.shadowPasses;
            result.data.cascadeMatrices[c] = cascade.viewProjection;
            result.data.cascadeSplits[c] = splits[static_cast<size_t>(c)];
            result.data.cascadeTexels[c] = cascade.texelSize;
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
        result.data.params = glm::vec4(static_cast<float>(kCascades), 1.0f / static_cast<float>(resolution), 0.0f, distance);
        result.cascadeLight = static_cast<int>(i);
        break;
    }

    // --- Pontuais e spots: as mais perto da câmera que iluminam algo visível ---
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
        int resolution = 1;
        for (const Candidate& c : candidates) {
            resolution = std::max(resolution, ShadowResolution(lights[c.light].type, lights[c.light].shadowQuality));
        }
        EnsureCubes(resolution);
        glViewport(0, 0, resolution, resolution);
        for (size_t slot = 0; slot < candidates.size(); ++slot) {
            const Light& light = lights[candidates[slot].light];
            const glm::mat4 projection = CubeFaceProjection(kPointNear, light.range);
            const glm::vec4 positionRange(light.position, light.range);
            for (int face = 0; face < 6; ++face) {
                const glm::mat4 viewProjection = projection * CubeFaceView(light.position, face);
                glNamedFramebufferTextureLayer(m_fbo, GL_DEPTH_ATTACHMENT, m_cubes, 0, static_cast<int>(slot) * 6 + face);
                glClearNamedFramebufferfv(m_fbo, GL_DEPTH, 0, &clearDepth);
                DrawItems(items, viewProjection, Frustum::FromMatrix(viewProjection), true, positionRange, uploadJoints, stats);
                ++stats.shadowPasses;
            }
            result.cubeLayer[candidates[slot].light] = static_cast<int>(slot);
        }
        result.data.params.z = 1.0f / static_cast<float>(resolution);
    }

    glNamedFramebufferTexture(m_fbo, GL_DEPTH_ATTACHMENT, 0, 0);
    return result;
}

} // namespace motor
