#include "renderer/PostProcess.h"

#include "motor/core/Log.h"
#include "motor/renderer/GpuMemory.h"
#include "renderer/GL.h"
#include "renderer/GpuProfile.h"

#include <glm/vec2.hpp>

#include <algorithm>

namespace motor {

namespace {
constexpr int kMaxBloomLevels = 6;
// Cor da cena em HDR: 32 bits por pixel (sem alfa, que ninguém lê), metade da banda do RGBA16F em cada passada que
// grava ou lê a cena (como o HDR padrão do URP da Unity). Basta para a luz até ~65000 e para o degradê do céu.
constexpr GLenum kSceneColorFormat = GL_R11F_G11F_B10F;

GLuint CreateTexture2D(GLenum format, int width, int height) {
    GLuint texture = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &texture);
    glTextureStorage2D(texture, 1, format, width, height);
    TrackGpuMemory(GpuObject::Texture, texture, GpuMemoryKind::PostProcess, GpuTextureBytes(format, width, height));
    glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

GLuint CreateColorFbo(GLuint texture) {
    GLuint fbo = 0;
    glCreateFramebuffers(1, &fbo);
    glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, texture, 0);
    if (glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log::Error("Pós-processamento: framebuffer incompleto");
    }
    return fbo;
}
} // namespace

PostProcess::PostProcess(const std::filesystem::path& dir) {
    m_bloomDown = Shader::FromFiles(dir / "fullscreen.vert", dir / "bloom_down.frag");
    m_bloomUp = Shader::FromFiles(dir / "fullscreen.vert", dir / "bloom_up.frag");
    m_composite = Shader::FromFiles(dir / "fullscreen.vert", dir / "composite.frag");
    m_fxaa = Shader::FromFiles(dir / "fullscreen.vert", dir / "fxaa.frag");
    glCreateVertexArrays(1, &m_emptyVao);
}

PostProcess::~PostProcess() {
    Destroy();
    glDeleteVertexArrays(1, &m_emptyVao);
}

void PostProcess::Resize(int width, int height, int samples) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    samples = std::max(samples, 1);
    if (width == m_width && height == m_height && samples == m_samples && m_sceneFbo) return;
    Destroy();
    m_width = width;
    m_height = height;
    m_samples = samples;
    Create();
}

void PostProcess::Create() {
    const bool msaa = m_samples > 1;
    m_colorTexture = CreateTexture2D(kSceneColorFormat, m_width, m_height);
    m_depthTexture = CreateTexture2D(GL_DEPTH_COMPONENT32F, m_width, m_height);
    glTextureParameteri(m_depthTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_depthTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glCreateFramebuffers(1, &m_sceneFbo);
    if (msaa) {
        glCreateRenderbuffers(1, &m_colorRbo);
        glNamedRenderbufferStorageMultisample(m_colorRbo, m_samples, kSceneColorFormat, m_width, m_height);
        TrackGpuMemory(GpuObject::Renderbuffer, m_colorRbo, GpuMemoryKind::PostProcess,
                       GpuTextureBytes(kSceneColorFormat, m_width, m_height, 1, 1, m_samples));
        glCreateRenderbuffers(1, &m_depthRbo);
        glNamedRenderbufferStorageMultisample(m_depthRbo, m_samples, GL_DEPTH_COMPONENT32F, m_width, m_height);
        TrackGpuMemory(GpuObject::Renderbuffer, m_depthRbo, GpuMemoryKind::PostProcess,
                       GpuTextureBytes(GL_DEPTH_COMPONENT32F, m_width, m_height, 1, 1, m_samples));
        glNamedFramebufferRenderbuffer(m_sceneFbo, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_colorRbo);
        glNamedFramebufferRenderbuffer(m_sceneFbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRbo);

        glCreateFramebuffers(1, &m_resolveFbo);
        glNamedFramebufferTexture(m_resolveFbo, GL_COLOR_ATTACHMENT0, m_colorTexture, 0);
        glNamedFramebufferTexture(m_resolveFbo, GL_DEPTH_ATTACHMENT, m_depthTexture, 0);
    } else {
        glNamedFramebufferTexture(m_sceneFbo, GL_COLOR_ATTACHMENT0, m_colorTexture, 0);
        glNamedFramebufferTexture(m_sceneFbo, GL_DEPTH_ATTACHMENT, m_depthTexture, 0);
    }
    if (glCheckNamedFramebufferStatus(m_sceneFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
        (msaa && glCheckNamedFramebufferStatus(m_resolveFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)) {
        log::Error("Cena HDR: framebuffer incompleto ({}x{}, {} amostras)", m_width, m_height, m_samples);
    }

    int w = m_width, h = m_height;
    for (int i = 0; i < kMaxBloomLevels; ++i) {
        w = std::max(w / 2, 1);
        h = std::max(h / 2, 1);
        if (i > 0 && std::min(w, h) < 4) break;
        BloomLevel level;
        level.width = w;
        level.height = h;
        level.texture = CreateTexture2D(GL_R11F_G11F_B10F, w, h);
        level.fbo = CreateColorFbo(level.texture);
        m_bloom.push_back(level);
    }

    m_ldrTexture = CreateTexture2D(GL_RGBA8, m_width, m_height);
    m_ldrFbo = CreateColorFbo(m_ldrTexture);

    if (m_refraction) CreateRefraction();
}

void PostProcess::CreateRefraction() {
    m_refractColor = CreateTexture2D(kSceneColorFormat, m_width, m_height);
    m_refractDepth = CreateTexture2D(GL_DEPTH_COMPONENT32F, m_width, m_height);
    glTextureParameteri(m_refractDepth, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_refractDepth, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_refractFbo = CreateColorFbo(m_refractColor);
    glNamedFramebufferTexture(m_refractFbo, GL_DEPTH_ATTACHMENT, m_refractDepth, 0);
}

void PostProcess::Destroy() {
    for (const BloomLevel& level : m_bloom) {
        glDeleteFramebuffers(1, &level.fbo);
        UntrackGpuMemory(GpuObject::Texture, level.texture);
        glDeleteTextures(1, &level.texture);
    }
    m_bloom.clear();
    if (m_ldrFbo) glDeleteFramebuffers(1, &m_ldrFbo);
    UntrackGpuMemory(GpuObject::Texture, m_ldrTexture);
    UntrackGpuMemory(GpuObject::Texture, m_colorTexture);
    UntrackGpuMemory(GpuObject::Texture, m_depthTexture);
    UntrackGpuMemory(GpuObject::Renderbuffer, m_colorRbo);
    UntrackGpuMemory(GpuObject::Renderbuffer, m_depthRbo);
    if (m_ldrTexture) glDeleteTextures(1, &m_ldrTexture);
    if (m_sceneFbo) glDeleteFramebuffers(1, &m_sceneFbo);
    if (m_resolveFbo) glDeleteFramebuffers(1, &m_resolveFbo);
    if (m_colorRbo) glDeleteRenderbuffers(1, &m_colorRbo);
    if (m_depthRbo) glDeleteRenderbuffers(1, &m_depthRbo);
    if (m_colorTexture) glDeleteTextures(1, &m_colorTexture);
    if (m_depthTexture) glDeleteTextures(1, &m_depthTexture);
    UntrackGpuMemory(GpuObject::Texture, m_refractColor);
    UntrackGpuMemory(GpuObject::Texture, m_refractDepth);
    if (m_refractFbo) glDeleteFramebuffers(1, &m_refractFbo);
    if (m_refractColor) glDeleteTextures(1, &m_refractColor);
    if (m_refractDepth) glDeleteTextures(1, &m_refractDepth);
    m_refractFbo = m_refractColor = m_refractDepth = 0;
    m_ldrFbo = m_ldrTexture = m_sceneFbo = m_resolveFbo = m_colorRbo = m_depthRbo = 0;
    m_colorTexture = m_depthTexture = 0;
}

void PostProcess::BeginScene() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);
    glViewport(0, 0, m_width, m_height);
    const float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const float clearDepth = 1.0f;
    glClearNamedFramebufferfv(m_sceneFbo, GL_COLOR, 0, clearColor);
    glClearNamedFramebufferfv(m_sceneFbo, GL_DEPTH, 0, &clearDepth);
}

void PostProcess::Resolve(bool depth) {
    if (!m_resolveFbo) return;
    MOTOR_PROFILE_GPU("Resolve MSAA");
    glBlitNamedFramebuffer(m_sceneFbo, m_resolveFbo, 0, 0, m_width, m_height, 0, 0, m_width, m_height,
                           GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (depth) {
        glBlitNamedFramebuffer(m_sceneFbo, m_resolveFbo, 0, 0, m_width, m_height, 0, 0, m_width, m_height,
                               GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    }
}

void PostProcess::CopyForRefraction() {
    if (!m_refractFbo) {
        m_refraction = true;  // daqui em diante, recriadas com os outros alvos
        CreateRefraction();
    }
    MOTOR_PROFILE_GPU("Cópia para a água");
    // Com MSAA o blit já resolve as amostras (a profundidade pela mais próxima: GL_NEAREST).
    glBlitNamedFramebuffer(m_sceneFbo, m_refractFbo, 0, 0, m_width, m_height, 0, 0, m_width, m_height,
                           GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);
    glViewport(0, 0, m_width, m_height);
}

void PostProcess::DrawFullscreen() const {
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void PostProcess::Apply(const Environment& env, const std::function<void()>& bindTarget) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    const bool bloom = env.bloom && env.bloomIntensity > 0.0f && !m_bloom.empty();
    if (bloom) {
        MOTOR_PROFILE_GPU("Bloom");
        // Descida: cena -> metade (com o limiar) -> um quarto -> ...
        m_bloomDown->Bind();
        m_bloomDown->SetFloat("uThreshold", std::max(env.bloomThreshold, 0.0f));
        for (size_t i = 0; i < m_bloom.size(); ++i) {
            const BloomLevel& level = m_bloom[i];
            const int srcW = i == 0 ? m_width : m_bloom[i - 1].width;
            const int srcH = i == 0 ? m_height : m_bloom[i - 1].height;
            glBindFramebuffer(GL_FRAMEBUFFER, level.fbo);
            glViewport(0, 0, level.width, level.height);
            glBindTextureUnit(0, i == 0 ? m_colorTexture : m_bloom[i - 1].texture);
            m_bloomDown->SetVec2("uTexel", glm::vec2(1.0f / static_cast<float>(srcW), 1.0f / static_cast<float>(srcH)));
            m_bloomDown->SetInt("uPrefilter", i == 0 ? 1 : 0);
            DrawFullscreen();
        }
        // Subida: cada nível menor, borrado, somado ao maior.
        m_bloomUp->Bind();
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        for (size_t i = m_bloom.size() - 1; i-- > 0;) {
            const BloomLevel& level = m_bloom[i];
            const BloomLevel& source = m_bloom[i + 1];
            glBindFramebuffer(GL_FRAMEBUFFER, level.fbo);
            glViewport(0, 0, level.width, level.height);
            glBindTextureUnit(0, source.texture);
            m_bloomUp->SetVec2("uTexel", glm::vec2(1.0f / static_cast<float>(source.width), 1.0f / static_cast<float>(source.height)));
            DrawFullscreen();
        }
        glDisable(GL_BLEND);
    }

    if (env.fxaa) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_ldrFbo);
        glViewport(0, 0, m_width, m_height);
    } else {
        bindTarget();
    }
    {
        MOTOR_PROFILE_GPU("Tone mapping");
        m_composite->Bind();
        glBindTextureUnit(0, m_colorTexture);
        glBindTextureUnit(1, bloom ? m_bloom[0].texture : m_colorTexture);
        m_composite->SetFloat("uExposure", std::max(env.exposure, 0.0f));
        // Soma dos níveis: dividida pelo número deles para a intensidade não depender do tamanho da tela.
        m_composite->SetFloat("uBloomIntensity", bloom ? env.bloomIntensity / static_cast<float>(m_bloom.size()) : 0.0f);
        m_composite->SetInt("uToneMapper", static_cast<int>(env.toneMapper));
        DrawFullscreen();
    }

    if (env.fxaa) {
        MOTOR_PROFILE_GPU("FXAA");
        bindTarget();
        m_fxaa->Bind();
        glBindTextureUnit(0, m_ldrTexture);
        m_fxaa->SetVec2("uTexel", glm::vec2(1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height)));
        DrawFullscreen();
    }
    glDepthMask(GL_TRUE);
}

} // namespace motor
