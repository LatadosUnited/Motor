#pragma once

#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>

namespace motor {

enum class TextureFilter { Linear, Nearest };
// Srgb: cor (albedo), convertida para linear na amostragem. Linear: dados ou imagens para a UI.
enum class TextureColorSpace { Srgb, Linear };

class Texture2D {
public:
    // pixels: RGBA 8 bits por canal, width*height*4 bytes, linha 0 = base da imagem.
    Texture2D(int width, int height, const uint8_t* pixels, TextureFilter filter = TextureFilter::Linear,
              TextureColorSpace colorSpace = TextureColorSpace::Srgb);
    ~Texture2D();

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    void Bind(unsigned int unit) const;

    int Width() const { return m_width; }
    int Height() const { return m_height; }
    unsigned int NativeHandle() const { return m_id; }  // nome OpenGL (ex.: para ImGui::Image)

    static std::shared_ptr<Texture2D> CreateSolid(glm::u8vec4 color);
    static std::shared_ptr<Texture2D> CreateChecker(int size, int cells, glm::u8vec4 a, glm::u8vec4 b);

private:
    unsigned int m_id = 0;
    int m_width = 0;
    int m_height = 0;
};

} // namespace motor
