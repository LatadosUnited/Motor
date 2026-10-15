#include "motor/renderer/Texture.h"

#include "renderer/GL.h"

#include <algorithm>
#include <bit>
#include <vector>

namespace motor {

Texture2D::Texture2D(int width, int height, const uint8_t* pixels, TextureFilter filter, TextureColorSpace colorSpace)
    : m_width(width), m_height(height) {
    const auto largest = static_cast<unsigned>(std::max(width, height));
    const GLsizei levels = static_cast<GLsizei>(std::bit_width(largest));  // 1 + floor(log2)

    glCreateTextures(GL_TEXTURE_2D, 1, &m_id);
    glTextureStorage2D(m_id, levels, colorSpace == TextureColorSpace::Srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, width, height);
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
    glDeleteTextures(1, &m_id);
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
