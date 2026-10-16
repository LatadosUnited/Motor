#pragma once

// Desenho 2D por cima da cena (HUD do jogo, Fase 9): quadriláteros texturizados e texto com fonte SDF, em
// lotes. A lista de desenho (DrawList2D) é montada na CPU, sem OpenGL; o Renderer2D a desenha com um único
// VBO dinâmico, uma chamada de desenho por troca de textura.
//
// Coordenadas em pixels do alvo, com a origem no canto superior esquerdo e o y para baixo.

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace motor {

class Framebuffer;
class Shader;
class Texture2D;

struct Vertex2D {
    glm::vec2 position{0.0f};  // pixels
    glm::vec2 uv{0.0f};
    glm::vec4 color{1.0f};     // multiplica a textura (sRGB, como os seletores de cor do editor)
    float sdf = 0.0f;          // 1 = texto: o alfa da textura é a distância até a borda do glifo (SDF)
};

// Quadriláteros seguidos com a mesma textura: uma chamada de desenho.
struct DrawBatch2D {
    const Texture2D* texture = nullptr;  // nulo = branco
    uint32_t firstQuad = 0;
    uint32_t quadCount = 0;
};

class DrawList2D {
public:
    void Clear();

    // Quadrilátero alinhado aos eixos. uvMin fica no canto superior esquerdo.
    void AddQuad(const Texture2D* texture, const glm::vec2& min, const glm::vec2& max, const glm::vec2& uvMin,
                 const glm::vec2& uvMax, const glm::vec4& color, bool sdf = false);
    // Quadriláteros prontos: 4 vértices cada (superior esquerdo, superior direito, inferior direito, inferior esquerdo).
    void AddQuads(const Texture2D* texture, std::span<const Vertex2D> vertices);

    const std::vector<Vertex2D>& Vertices() const { return m_vertices; }
    const std::vector<DrawBatch2D>& Batches() const { return m_batches; }
    size_t QuadCount() const { return m_vertices.size() / 4; }
    bool Empty() const { return m_vertices.empty(); }

private:
    void Extend(const Texture2D* texture, uint32_t quads);

    std::vector<Vertex2D> m_vertices;
    std::vector<DrawBatch2D> m_batches;
};

class Renderer2D {
public:
    Renderer2D();  // exige o contexto OpenGL (depois do Renderer, que carrega as funções)
    ~Renderer2D();

    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    // Desenha a lista por cima do que já está no alvo (sem limpar, sem profundidade, com transparência).
    // target == nullptr: a janela, com width x height. `upload = false` reaproveita os vértices enviados na
    // chamada anterior (a lista não mudou): nada vai para a GPU.
    void Render(const DrawList2D& list, int width, int height, const Framebuffer* target = nullptr, bool upload = true);

    uint32_t LastDrawCalls() const { return m_drawCalls; }
    uint64_t UploadCount() const { return m_uploads; }  // quantas vezes os vértices foram enviados

private:
    void Reserve(size_t quads);

    std::unique_ptr<Shader> m_shader;
    std::shared_ptr<Texture2D> m_white;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;
    size_t m_capacity = 0;  // em quadriláteros
    uint32_t m_drawCalls = 0;
    uint64_t m_uploads = 0;
};

} // namespace motor
