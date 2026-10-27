#pragma once

#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>
#include <vector>

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
    unsigned int NativeHandle() const {  // nome OpenGL (ex.: para ImGui::Image)
        if (!m_pending.empty()) Upload();
        return m_id;
    }
    // Mesma imagem lida sem a conversão sRGB (mapas de normal, metal/rugosidade e oclusão guardam dados, não
    // cor). Textura sRGB: uma "view" OpenGL criada no primeiro uso; textura linear: a própria.
    unsigned int LinearHandle() const;
    TextureColorSpace ColorSpace() const { return m_colorSpace; }  // a UI (Renderer2D) devolve as sRGB para sRGB

    // Textura de dados (pesos do terreno, Fase 28) criada sem OpenGL: os pixels ficam guardados e vão para a GPU no
    // primeiro uso (NativeHandle, LinearHandle, Bind), como a Mesh. Linear, borda presa (sem repetir o outro lado).
    static std::shared_ptr<Texture2D> CreateDeferred(int width, int height, std::vector<uint8_t> pixels);
    static std::shared_ptr<Texture2D> CreateSolid(glm::u8vec4 color);
    static std::shared_ptr<Texture2D> CreateChecker(int size, int cells, glm::u8vec4 a, glm::u8vec4 b);

private:
    Texture2D() = default;
    void Upload() const;  // textura adiada: cria no OpenGL agora

    mutable std::vector<uint8_t> m_pending;  // textura adiada ainda não enviada
    mutable unsigned int m_id = 0;
    mutable unsigned int m_linearView = 0;
    mutable int m_levels = 1;
    int m_width = 0;
    int m_height = 0;
    TextureColorSpace m_colorSpace = TextureColorSpace::Srgb;
};

} // namespace motor
