#include "motor/renderer/Mesh.h"

#include "renderer/GL.h"

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/geometric.hpp>

#include <cmath>
#include <cstddef>
#include <functional>
#include <vector>

namespace motor {

Mesh::Mesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices)
    : Mesh(vertices, indices, {}) {}

Mesh::Mesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices, std::span<const SkinWeights> skinWeights)
    : m_indices(indices.begin(), indices.end()), m_pendingVertices(vertices.begin(), vertices.end()) {
    m_positions.reserve(vertices.size());
    for (const Vertex& v : vertices) m_positions.push_back(v.position);
    if (!m_positions.empty()) {
        m_boundsMin = m_boundsMax = m_positions[0];
        for (const glm::vec3& p : m_positions) {
            m_boundsMin = glm::min(m_boundsMin, p);
            m_boundsMax = glm::max(m_boundsMax, p);
        }
    }
    if (skinWeights.size() == vertices.size()) m_weights.assign(skinWeights.begin(), skinWeights.end());
}

Mesh::~Mesh() {
    if (m_vao == 0) return;  // nunca foi desenhada: não há nada na GPU
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ibo);
    if (m_skinVbo) glDeleteBuffers(1, &m_skinVbo);
}

void Mesh::Upload() const {
    glCreateBuffers(1, &m_vbo);
    glNamedBufferStorage(m_vbo, static_cast<GLsizeiptr>(m_pendingVertices.size() * sizeof(Vertex)),
                         m_pendingVertices.data(), 0);

    glCreateBuffers(1, &m_ibo);
    glNamedBufferStorage(m_ibo, static_cast<GLsizeiptr>(m_indices.size() * sizeof(uint32_t)), m_indices.data(), 0);

    glCreateVertexArrays(1, &m_vao);
    glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(m_vao, m_ibo);

    struct Attr { GLuint location; GLint size; GLuint offset; GLuint binding; };
    std::vector<Attr> attrs = {
        {0, 3, offsetof(Vertex, position), 0},
        {1, 3, offsetof(Vertex, normal), 0},
        {2, 2, offsetof(Vertex, uv), 0},
    };
    // Pele num segundo buffer (binding 1): só o shader de malhas animadas lê as posições 3 e 4.
    if (!m_weights.empty()) {
        glCreateBuffers(1, &m_skinVbo);
        glNamedBufferStorage(m_skinVbo, static_cast<GLsizeiptr>(m_weights.size() * sizeof(SkinWeights)),
                             m_weights.data(), 0);
        glVertexArrayVertexBuffer(m_vao, 1, m_skinVbo, 0, sizeof(SkinWeights));
        attrs.push_back({3, 4, offsetof(SkinWeights, joints), 1});
        attrs.push_back({4, 4, offsetof(SkinWeights, weights), 1});
    }
    for (const auto& a : attrs) {
        glEnableVertexArrayAttrib(m_vao, a.location);
        glVertexArrayAttribFormat(m_vao, a.location, a.size, GL_FLOAT, GL_FALSE, a.offset);
        glVertexArrayAttribBinding(m_vao, a.location, a.binding);
    }
    m_pendingVertices = {};  // a GPU já tem; na CPU ficam só posições e índices
}

void Mesh::Draw() const {
    if (m_vao == 0) Upload();
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, nullptr);
}

std::shared_ptr<Mesh> Mesh::CreateCube() {
    // Cada face: normal n e eixos (u, v) com u x v = n, para winding anti-horário visto de fora.
    struct Face { glm::vec3 n, u, v; };
    const Face faces[] = {
        {{ 1, 0, 0}, { 0, 0, -1}, {0, 1,  0}},
        {{-1, 0, 0}, { 0, 0,  1}, {0, 1,  0}},
        {{ 0, 1, 0}, { 1, 0,  0}, {0, 0, -1}},
        {{ 0,-1, 0}, { 1, 0,  0}, {0, 0,  1}},
        {{ 0, 0, 1}, { 1, 0,  0}, {0, 1,  0}},
        {{ 0, 0,-1}, {-1, 0,  0}, {0, 1,  0}},
    };

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    for (const auto& f : faces) {
        const auto base = static_cast<uint32_t>(vertices.size());
        const glm::vec2 corners[] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (const auto& c : corners) {
            const glm::vec3 p = 0.5f * (f.n + c.x * f.u + c.y * f.v);
            vertices.push_back({p, f.n, (c + 1.0f) * 0.5f});
        }
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return std::make_shared<Mesh>(vertices, indices);
}

std::shared_ptr<Mesh> Mesh::CreatePlane(float uvRepeat) {
    const glm::vec3 n{0, 1, 0};
    const Vertex vertices[] = {
        {{-0.5f, 0,  0.5f}, n, {0, 0}},
        {{ 0.5f, 0,  0.5f}, n, {uvRepeat, 0}},
        {{ 0.5f, 0, -0.5f}, n, {uvRepeat, uvRepeat}},
        {{-0.5f, 0, -0.5f}, n, {0, uvRepeat}},
    };
    const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
    return std::make_shared<Mesh>(vertices, indices);
}

std::shared_ptr<Mesh> Mesh::CreateSphere(int segments, int rings) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(static_cast<size_t>((segments + 1) * (rings + 1)));

    for (int r = 0; r <= rings; ++r) {
        const float phi = glm::pi<float>() * static_cast<float>(r) / static_cast<float>(rings);
        for (int s = 0; s <= segments; ++s) {
            const float theta = glm::two_pi<float>() * static_cast<float>(s) / static_cast<float>(segments);
            const glm::vec3 n{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
            vertices.push_back({n * 0.5f, n,
                                {static_cast<float>(s) / segments, 1.0f - static_cast<float>(r) / rings}});
        }
    }

    const auto stride = static_cast<uint32_t>(segments + 1);
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            const uint32_t a = static_cast<uint32_t>(r) * stride + static_cast<uint32_t>(s);
            const uint32_t b = a + stride;
            indices.insert(indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    }
    return std::make_shared<Mesh>(vertices, indices);
}


namespace {
// Anel de `segments` + 1 vértices em volta do eixo Y, na altura `y` (o último repete o primeiro, para a costura
// da textura). Mesma ordem de ângulos da esfera: x = cos, z = sin.
void Ring(std::vector<Vertex>& vertices, int segments, float radius, float y, float v,
          const std::function<glm::vec3(float cosT, float sinT)>& normal) {
    for (int s = 0; s <= segments; ++s) {
        const float theta = glm::two_pi<float>() * static_cast<float>(s) / static_cast<float>(segments);
        const float c = std::cos(theta), sn = std::sin(theta);
        vertices.push_back({{c * radius, y, sn * radius}, normal(c, sn), {static_cast<float>(s) / segments, v}});
    }
}

// Faixa entre dois anéis seguidos (o de cima começa em `top`), com o mesmo sentido dos triângulos da esfera.
void Band(std::vector<uint32_t>& indices, uint32_t top, int segments) {
    const auto stride = static_cast<uint32_t>(segments + 1);
    for (uint32_t s = 0; s < static_cast<uint32_t>(segments); ++s) {
        const uint32_t a = top + s, b = a + stride;
        indices.insert(indices.end(), {a, a + 1, b, a + 1, b + 1, b});
    }
}

// Tampa plana no eixo Y: centro + anel. `up`: normal +Y (tampa de cima) ou -Y (de baixo).
void Cap(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, int segments, float radius, float y, bool up) {
    const auto center = static_cast<uint32_t>(vertices.size());
    const glm::vec3 n{0.0f, up ? 1.0f : -1.0f, 0.0f};
    vertices.push_back({{0.0f, y, 0.0f}, n, {0.5f, 0.5f}});
    for (int s = 0; s <= segments; ++s) {
        const float theta = glm::two_pi<float>() * static_cast<float>(s) / static_cast<float>(segments);
        const float c = std::cos(theta), sn = std::sin(theta);
        vertices.push_back({{c * radius, y, sn * radius}, n, {0.5f + 0.5f * c, 0.5f + 0.5f * sn}});
    }
    for (uint32_t s = 0; s < static_cast<uint32_t>(segments); ++s) {
        const uint32_t v0 = center + 1 + s, v1 = v0 + 1;
        if (up) indices.insert(indices.end(), {center, v1, v0});
        else indices.insert(indices.end(), {center, v0, v1});
    }
}
} // namespace

std::shared_ptr<Mesh> Mesh::CreateCylinder(int segments) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    auto side = [](float c, float s) { return glm::vec3(c, 0.0f, s); };
    Ring(vertices, segments, 0.5f, 1.0f, 1.0f, side);
    Ring(vertices, segments, 0.5f, -1.0f, 0.0f, side);
    Band(indices, 0, segments);
    Cap(vertices, indices, segments, 0.5f, 1.0f, true);
    Cap(vertices, indices, segments, 0.5f, -1.0f, false);
    return std::make_shared<Mesh>(vertices, indices);
}

std::shared_ptr<Mesh> Mesh::CreateCapsule(int segments, int rings) {
    // Duas meias esferas de raio 0,5 separadas por 1 (altura total 2), como a cápsula do Collider padrão.
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    const float radius = 0.5f, half = 0.5f;
    for (int r = 0; r <= 2 * rings + 1; ++r) {  // 2 * rings + 2 anéis: o equador repetido
        const bool topHalf = r <= rings;
        const int k = topHalf ? r : r - 1;  // o equador aparece duas vezes: em cima e embaixo do cilindro
        const float phi = glm::pi<float>() * 0.5f * static_cast<float>(k) / static_cast<float>(rings);
        const float y = std::cos(phi) * radius + (topHalf ? half : -half);
        const float ringRadius = std::sin(phi) * radius;
        const float cosPhi = std::cos(phi), sinPhi = std::sin(phi);
        Ring(vertices, segments, ringRadius, y, (y + 1.0f) * 0.5f,
             [cosPhi, sinPhi](float c, float s) { return glm::vec3(sinPhi * c, cosPhi, sinPhi * s); });
        if (r > 0) Band(indices, static_cast<uint32_t>((r - 1) * (segments + 1)), segments);
    }
    return std::make_shared<Mesh>(vertices, indices);
}

std::shared_ptr<Mesh> Mesh::CreateCone(int segments) {
    // Base de raio 0,5 em y = -0,5 e ponta em y = 0,5. Cada fatia tem a sua ponta, com a normal do meio dela.
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    const float radius = 0.5f, height = 1.0f;
    auto slope = [&](float c, float s) { return glm::normalize(glm::vec3(c * height, radius, s * height)); };
    Ring(vertices, segments, radius, -0.5f, 0.0f, slope);
    for (int s = 0; s < segments; ++s) {
        const float theta = glm::two_pi<float>() * (static_cast<float>(s) + 0.5f) / static_cast<float>(segments);
        const auto apex = static_cast<uint32_t>(vertices.size());
        vertices.push_back({{0.0f, 0.5f, 0.0f}, slope(std::cos(theta), std::sin(theta)),
                            {(static_cast<float>(s) + 0.5f) / segments, 1.0f}});
        indices.insert(indices.end(), {apex, static_cast<uint32_t>(s + 1), static_cast<uint32_t>(s)});
    }
    Cap(vertices, indices, segments, radius, -0.5f, false);
    return std::make_shared<Mesh>(vertices, indices);
}

} // namespace motor
