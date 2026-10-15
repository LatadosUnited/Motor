#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <span>

namespace motor {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

class Mesh {
public:
    Mesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices);
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void Draw() const;

    uint32_t IndexCount() const { return m_indexCount; }

    // Caixa alinhada aos eixos em espaço local.
    const glm::vec3& BoundsMin() const { return m_boundsMin; }
    const glm::vec3& BoundsMax() const { return m_boundsMax; }

    // Primitivas (todas centradas na origem, tamanho 1).
    static std::shared_ptr<Mesh> CreateCube();
    static std::shared_ptr<Mesh> CreatePlane(float uvRepeat = 1.0f);  // plano XZ, normal +Y
    static std::shared_ptr<Mesh> CreateSphere(int segments = 48, int rings = 24);

private:
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ibo = 0;
    uint32_t m_indexCount = 0;
    glm::vec3 m_boundsMin{0.0f};
    glm::vec3 m_boundsMax{0.0f};
};

} // namespace motor
