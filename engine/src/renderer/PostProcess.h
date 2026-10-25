#pragma once

// Header privado do renderer. Alvos da cena em HDR (com MSAA) e o pós-processamento: bloom, tone mapping e FXAA.

#include "motor/renderer/Environment.h"
#include "motor/renderer/Shader.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace motor {

class PostProcess {
public:
    explicit PostProcess(const std::filesystem::path& shaderDir);
    ~PostProcess();

    PostProcess(const PostProcess&) = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    // Recria os alvos se o tamanho ou o MSAA mudou.
    void Resize(int width, int height, int samples);
    int Width() const { return m_width; }
    int Height() const { return m_height; }

    // Vincula o alvo HDR da cena (viewport incluído) e limpa cor e profundidade.
    void BeginScene();
    // MSAA -> textura de cor (SceneColor passa a valer) e, com `depth`, a de profundidade (SceneDepth).
    void Resolve(bool depth);
    // Bloom + tone mapping (+ FXAA) da cor resolvida para o destino final (do mesmo tamanho), que
    // `bindTarget` vincula (framebuffer e viewport). Mexe no estado do GL: blend, profundidade e viewport.
    void Apply(const Environment& environment, const std::function<void()>& bindTarget);

    unsigned int SceneColor() const { return m_colorTexture; }  // R11G11B10F linear
    unsigned int SceneDepth() const { return m_depthTexture; }  // DEPTH_COMPONENT32F, valores do depth buffer

    // Triângulo de tela cheia (fullscreen.vert): VAO vazio + 3 vértices.
    void DrawFullscreen() const;

private:
    void Create();
    void Destroy();

    int m_width = 0;
    int m_height = 0;
    int m_samples = 1;

    unsigned int m_sceneFbo = 0;    // onde a cena é desenhada (MSAA ou direto nas texturas)
    unsigned int m_resolveFbo = 0;  // só com MSAA
    unsigned int m_colorRbo = 0, m_depthRbo = 0;
    unsigned int m_colorTexture = 0, m_depthTexture = 0;

    struct BloomLevel {
        unsigned int texture = 0;
        unsigned int fbo = 0;
        int width = 0;
        int height = 0;
    };
    std::vector<BloomLevel> m_bloom;  // metade, um quarto... (texturas separadas: sem leitura e escrita na mesma)
    unsigned int m_ldrTexture = 0, m_ldrFbo = 0;  // imagem final antes do FXAA
    unsigned int m_emptyVao = 0;

    std::unique_ptr<Shader> m_bloomDown;
    std::unique_ptr<Shader> m_bloomUp;
    std::unique_ptr<Shader> m_composite;
    std::unique_ptr<Shader> m_fxaa;
};

} // namespace motor
