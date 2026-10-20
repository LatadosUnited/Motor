#include "motor/renderer/Renderer2D.h"

#include "motor/core/EngineAssets.h"
#include "motor/core/Log.h"
#include "motor/renderer/Framebuffer.h"
#include "motor/renderer/Shader.h"
#include "motor/renderer/Texture.h"

#include "renderer/GL.h"

#include <algorithm>
#include <cstddef>
#include <string>

namespace motor {

// --- DrawList2D (CPU) -----------------------------------------------------------------

void DrawList2D::Clear() {
    m_vertices.clear();
    m_batches.clear();
}

void DrawList2D::Extend(const Texture2D* texture, uint32_t quads) {
    if (!m_batches.empty() && m_batches.back().texture == texture) {
        m_batches.back().quadCount += quads;
        return;
    }
    const auto first = static_cast<uint32_t>(m_vertices.size() / 4) - quads;
    m_batches.push_back({texture, first, quads});
}

void DrawList2D::AddQuad(const Texture2D* texture, const glm::vec2& min, const glm::vec2& max, const glm::vec2& uvMin,
                         const glm::vec2& uvMax, const glm::vec4& color, bool sdf) {
    const float s = sdf ? 1.0f : 0.0f;
    m_vertices.push_back({{min.x, min.y}, {uvMin.x, uvMin.y}, color, s});
    m_vertices.push_back({{max.x, min.y}, {uvMax.x, uvMin.y}, color, s});
    m_vertices.push_back({{max.x, max.y}, {uvMax.x, uvMax.y}, color, s});
    m_vertices.push_back({{min.x, max.y}, {uvMin.x, uvMax.y}, color, s});
    Extend(texture, 1);
}

void DrawList2D::AddQuads(const Texture2D* texture, std::span<const Vertex2D> vertices) {
    const auto quads = static_cast<uint32_t>(vertices.size() / 4);
    if (quads == 0) return;
    m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.begin() + quads * 4);
    Extend(texture, quads);
}

// --- Renderer2D (GPU) -----------------------------------------------------------------

Renderer2D::Renderer2D() {
    const std::filesystem::path dir = EngineAssetsDir() / "shaders";
    m_shader = Shader::FromFiles(dir / "ui.vert", dir / "ui.frag");
    m_white = Texture2D::CreateSolid({255, 255, 255, 255});

    glCreateVertexArrays(1, &m_vao);
    struct Attr { GLuint location; GLint size; GLuint offset; };
    const Attr attrs[] = {
        {0, 2, offsetof(Vertex2D, position)},
        {1, 2, offsetof(Vertex2D, uv)},
        {2, 4, offsetof(Vertex2D, color)},
        {3, 1, offsetof(Vertex2D, sdf)},
    };
    for (const auto& a : attrs) {
        glEnableVertexArrayAttrib(m_vao, a.location);
        glVertexArrayAttribFormat(m_vao, a.location, a.size, GL_FLOAT, GL_FALSE, a.offset);
        glVertexArrayAttribBinding(m_vao, a.location, 0);
    }
    Reserve(256);
}

Renderer2D::~Renderer2D() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ebo);
}

void Renderer2D::Reserve(size_t quads) {
    if (quads <= m_capacity) return;
    // Cresce em dobro: refazer os buffers é raro (o armazenamento é imutável, então troca o buffer inteiro).
    size_t capacity = std::max<size_t>(m_capacity, 256);
    while (capacity < quads) capacity *= 2;

    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ebo);
    glCreateBuffers(1, &m_vbo);
    glNamedBufferStorage(m_vbo, static_cast<GLsizeiptr>(capacity * 4 * sizeof(Vertex2D)), nullptr,
                         GL_DYNAMIC_STORAGE_BIT);

    // Índices fixos: 2 triângulos por quadrilátero.
    std::vector<uint32_t> indices(capacity * 6);
    for (size_t q = 0; q < capacity; ++q) {
        const auto v = static_cast<uint32_t>(q * 4);
        const uint32_t quad[6] = {v, v + 1, v + 2, v, v + 2, v + 3};
        std::copy(std::begin(quad), std::end(quad), indices.begin() + static_cast<std::ptrdiff_t>(q * 6));
    }
    glCreateBuffers(1, &m_ebo);
    glNamedBufferStorage(m_ebo, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(), 0);

    glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, sizeof(Vertex2D));
    glVertexArrayElementBuffer(m_vao, m_ebo);
    m_capacity = capacity;
}

void Renderer2D::Render(const DrawList2D& list, int width, int height, const Framebuffer* target, bool upload) {
    m_drawCalls = 0;
    if (list.Empty() || width <= 0 || height <= 0) return;

    if (upload || list.QuadCount() > m_capacity) {
        Reserve(list.QuadCount());
        glNamedBufferSubData(m_vbo, 0, static_cast<GLsizeiptr>(list.Vertices().size() * sizeof(Vertex2D)),
                             list.Vertices().data());
        ++m_uploads;
    }

    if (target) {
        target->Bind();
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    m_shader->Bind();
    m_shader->SetVec2("uScreen", glm::vec2(static_cast<float>(width), static_cast<float>(height)));
    m_shader->SetInt("uTexture", 0);
    glBindVertexArray(m_vao);
    for (const DrawBatch2D& batch : list.Batches()) {
        const Texture2D* texture = batch.texture ? batch.texture : m_white.get();
        texture->Bind(0);
        m_shader->SetInt("uSrgbTexture", texture->ColorSpace() == TextureColorSpace::Srgb ? 1 : 0);
        // Com o intervalo de vértices (4 por quad): o driver não varre os índices na CPU (ver Mesh::Draw).
        const auto firstVertex = static_cast<GLuint>(batch.firstQuad * 4);
        glDrawRangeElements(GL_TRIANGLES, firstVertex, firstVertex + static_cast<GLuint>(batch.quadCount * 4) - 1,
                            static_cast<GLsizei>(batch.quadCount * 6), GL_UNSIGNED_INT,
                            reinterpret_cast<const void*>(static_cast<uintptr_t>(batch.firstQuad) * 6 * sizeof(uint32_t)));
        ++m_drawCalls;
    }

    // Devolve o estado que a cena espera (o Renderer reafirma no BeginScene, mas outros podem desenhar antes).
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    if (target) {
        target->Resolve();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

} // namespace motor
