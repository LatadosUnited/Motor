#include "motor/renderer/Framebuffer.h"

#include "motor/core/Log.h"
#include "renderer/GL.h"

#include <algorithm>

namespace motor {

Framebuffer::Framebuffer(int width, int height, int samples)
    : m_width(std::max(1, width)), m_height(std::max(1, height)), m_samples(std::max(1, samples)) {
    Create();
}

Framebuffer::~Framebuffer() {
    Destroy();
}

void Framebuffer::Resize(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (width == m_width && height == m_height) return;
    m_width = width;
    m_height = height;
    Destroy();
    Create();
}

void Framebuffer::Create() {
    const bool msaa = m_samples > 1;

    glCreateTextures(GL_TEXTURE_2D, 1, &m_colorTexture);
    glTextureStorage2D(m_colorTexture, 1, GL_RGBA8, m_width, m_height);
    glTextureParameteri(m_colorTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_colorTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_colorTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_colorTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glCreateRenderbuffers(1, &m_depthRbo);
    glNamedRenderbufferStorageMultisample(m_depthRbo, msaa ? m_samples : 0, GL_DEPTH24_STENCIL8, m_width, m_height);

    glCreateFramebuffers(1, &m_renderFbo);
    if (msaa) {
        glCreateRenderbuffers(1, &m_colorRbo);
        glNamedRenderbufferStorageMultisample(m_colorRbo, m_samples, GL_RGBA8, m_width, m_height);
        glNamedFramebufferRenderbuffer(m_renderFbo, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_colorRbo);

        glCreateFramebuffers(1, &m_resolveFbo);
        glNamedFramebufferTexture(m_resolveFbo, GL_COLOR_ATTACHMENT0, m_colorTexture, 0);
    } else {
        glNamedFramebufferTexture(m_renderFbo, GL_COLOR_ATTACHMENT0, m_colorTexture, 0);
    }
    glNamedFramebufferRenderbuffer(m_renderFbo, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthRbo);

    if (glCheckNamedFramebufferStatus(m_renderFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
        (msaa && glCheckNamedFramebufferStatus(m_resolveFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)) {
        log::Error("Framebuffer incompleto ({}x{}, {} samples)", m_width, m_height, m_samples);
    }
}

void Framebuffer::Destroy() {
    glDeleteFramebuffers(1, &m_renderFbo);
    glDeleteTextures(1, &m_colorTexture);
    glDeleteRenderbuffers(1, &m_depthRbo);
    if (m_resolveFbo) glDeleteFramebuffers(1, &m_resolveFbo);
    if (m_colorRbo) glDeleteRenderbuffers(1, &m_colorRbo);
    m_renderFbo = m_resolveFbo = m_colorRbo = m_depthRbo = m_colorTexture = 0;
}

void Framebuffer::Bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_renderFbo);
    glViewport(0, 0, m_width, m_height);
}

void Framebuffer::Resolve() const {
    if (!m_resolveFbo) return;
    glBlitNamedFramebuffer(m_renderFbo, m_resolveFbo, 0, 0, m_width, m_height,
                           0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

} // namespace motor
