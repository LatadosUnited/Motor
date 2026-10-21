#include "motor/renderer/Texture.h"

#include "motor/renderer/GpuMemory.h"
#include "renderer/GL.h"

#include <algorithm>
#include <bit>
#include <vector>

namespace motor {

Texture2D::Texture2D(int width, int height, const uint8_t* pixels, TextureFilter filter, TextureColorSpace colorSpace)
    : m_width(width), m_height(height), m_colorSpace(colorSpace) {
    const auto largest = static_cast<unsigned>(std::max(width, height));
    const GLsizei levels = static_cast<GLsizei>(std::bit_width(largest));  // 1 + floor(log2)
    m_levels = levels;

    const GLenum format = colorSpace == TextureColorSpace::Srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    glCreateTextures(GL_TEXTURE_2D, 1, &m_id);
    glTextureStorage2D(m_id, levels, format, width, height);
    TrackGpuMemory(GpuObject::Texture, m_id, GpuMemoryKind::Textures, GpuTextureBytes(format, width, height, 1, levels));
    glTextureSubImage2D(m_id, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    const bool nearest = filter == TextureFilter::Nearest;
    glTextureParameteri(m_id, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_id, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTextureParameteri(m_id, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_id, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameterf(m_id, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);  // core no 4.6, extensão quase universal no 4.5
    glGenerateTextureMipmap(m_id);
}

Texture2D::~Texture2D() {
    if (m_linearView) glDeleteTextures(1, &m_linearView);  // vista da mesma memória: não conta à parte
    UntrackGpuMemory(GpuObject::Texture, m_id);
    glDeleteTextures(1, &m_id);
}

unsigned int Texture2D::LinearHandle() const {
    if (m_colorSpace == TextureColorSpace::Linear) return m_id;
    if (!m_linearView) {
        // glTextureView exige um nome ainda sem tipo (glGenTextures, não glCreateTextures).
        glGenTextures(1, &m_linearView);
        glTextureView(m_linearView, GL_TEXTURE_2D, m_id, GL_RGBA8, 0, static_cast<GLuint>(m_levels), 0, 1);
        glTextureParameteri(m_linearView, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(m_linearView, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(m_linearView, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(m_linearView, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTextureParameterf(m_linearView, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
    }
    return m_linearView;
}

void Texture2D::Bind(unsigned int unit) const {
    glBindTextureUnit(unit, m_id);
}

std::shared_ptr<Texture2D> Texture2D::CreateSolid(glm::u8vec4 color) {
    return std::make_shared<Texture2D>(1, 1, &color.x);
}

std::shared_ptr<Texture2D> Texture2D::CreateChecker(int size, int cells, glm::u8vec4 a, glm::u8vec4 b) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size * size * 4));
    const int cell = std::max(1, size / cells);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const glm::u8vec4& c = ((x / cell + y / cell) % 2 == 0) ? a : b;
            uint8_t* p = &pixels[static_cast<size_t>((y * size + x) * 4)];
            p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
        }
    }
    return std::make_shared<Texture2D>(size, size, pixels.data());
}

} // namespace motor
