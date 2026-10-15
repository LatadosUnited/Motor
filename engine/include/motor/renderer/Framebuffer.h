#pragma once

namespace motor {

// Alvo de renderização fora da tela (ex.: viewport do editor).
// Com samples > 1 renderiza em renderbuffers MSAA e resolve para a textura de cor.
class Framebuffer {
public:
    Framebuffer(int width, int height, int samples = 4);
    ~Framebuffer();

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    void Resize(int width, int height);  // no-op se o tamanho não mudou

    void Bind() const;     // vincula e ajusta o viewport
    void Resolve() const;  // MSAA -> textura de cor (no-op sem MSAA)

    int Width() const { return m_width; }
    int Height() const { return m_height; }
    float AspectRatio() const { return m_height > 0 ? static_cast<float>(m_width) / m_height : 1.0f; }

    // Textura RGBA8 com o resultado (válida após Resolve()).
    unsigned int ColorTexture() const { return m_colorTexture; }

private:
    void Create();
    void Destroy();

    int m_width = 0;
    int m_height = 0;
    int m_samples = 1;

    unsigned int m_renderFbo = 0;    // onde se desenha (MSAA ou não)
    unsigned int m_resolveFbo = 0;   // só com MSAA
    unsigned int m_colorRbo = 0;     // só com MSAA
    unsigned int m_depthRbo = 0;
    unsigned int m_colorTexture = 0;
};

} // namespace motor
