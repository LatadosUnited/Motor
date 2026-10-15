#include "motor/renderer/Mesh.h"

#include "renderer/GL.h"

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/geometric.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

namespace motor {

Mesh::Mesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices)
    : m_indexCount(static_cast<uint32_t>(indices.size())) {
    if (!vertices.empty()) {
        m_boundsMin = m_boundsMax = vertices[0].position;
        for (const Vertex& v : vertices) {
            m_boundsMin = glm::min(m_boundsMin, v.position);
            m_boundsMax = glm::max(m_boundsMax, v.position);
        }
    }

    glCreateBuffers(1, &m_vbo);
    glNamedBufferStorage(m_vbo, static_cast<GLsizeiptr>(vertices.size_bytes()), vertices.data(), 0);

    glCreateBuffers(1, &m_ibo);
    glNamedBufferStorage(m_ibo, static_cast<GLsizeiptr>(indices.size_bytes()), indices.data(), 0);

    glCreateVertexArrays(1, &m_vao);
    glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(m_vao, m_ibo);

    struct Attr { GLuint location; GLint size; GLuint offset; };
    const Attr attrs[] = {
        {0, 3, offsetof(Vertex, position)},
        {1, 3, offsetof(Vertex, normal)},
        {2, 2, offsetof(Vertex, uv)},
    };
    for (const auto& a : attrs) {
        glEnableVertexArrayAttrib(m_vao, a.location);
        glVertexArrayAttribFormat(m_vao, a.location, a.size, GL_FLOAT, GL_FALSE, a.offset);
        glVertexArrayAttribBinding(m_vao, a.location, 0);
    }
}

Mesh::~Mesh() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ibo);
}

void Mesh::Draw() const {
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indexCount), GL_UNSIGNED_INT, nullptr);
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

} // namespace motor
