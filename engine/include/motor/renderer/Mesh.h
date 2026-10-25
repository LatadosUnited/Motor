#pragma once

#include "motor/renderer/Skeleton.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace motor {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

// Malha de triângulos. Guarda uma cópia das posições e dos índices na CPU (raycast exato do editor, collider
// de malha da física) e manda os vértices para a GPU no primeiro Draw: criar uma Mesh não precisa de OpenGL
// (os testes criam malhas sem janela).
class Mesh {
public:
    Mesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices);
    // Malha com pele (animação esquelética): uma influência por vértice, na mesma ordem de `vertices`.
    Mesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices, std::span<const SkinWeights> skinWeights);
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void Draw() const;
    // `count` cópias numa chamada só (instancing, Fase 23): cada uma acha os seus dados por gl_InstanceID.
    void DrawInstanced(uint32_t count) const;

    uint32_t IndexCount() const { return static_cast<uint32_t>(m_indices.size()); }

    // Caixa alinhada aos eixos em espaço local.
    const glm::vec3& BoundsMin() const { return m_boundsMin; }
    const glm::vec3& BoundsMax() const { return m_boundsMax; }

    // Cópia na CPU (espaço local; malhas com pele: na pose de "bind").
    const std::vector<glm::vec3>& Positions() const { return m_positions; }
    const std::vector<uint32_t>& Indices() const { return m_indices; }

    // Pele: juntas que movem os vértices. Sem pele (ou sem pesos), a malha é desenhada parada.
    const std::vector<SkinWeights>& Weights() const { return m_weights; }
    const std::shared_ptr<const Skin>& GetSkin() const { return m_skin; }
    void SetSkin(std::shared_ptr<const Skin> skin) { m_skin = std::move(skin); }
    bool IsSkinned() const { return m_skin && !m_weights.empty(); }

    // Níveis de detalhe (LOD, Fase 24): versões mais simples da mesma malha para quando o objeto fica pequeno na
    // tela. `screenSize` = diâmetro da esfera dos limites do objeto dividido pela altura vista pela câmera na
    // distância dele (1 = ocupa a tela de cima a baixo); o nível vale abaixo desse valor. O Renderer escolhe no
    // Submit (também para as sombras); o resto da engine (raycast, física, editor) usa sempre esta malha.
    struct Lod {
        std::shared_ptr<Mesh> mesh;
        float screenSize = 0.0f;
    };
    void SetLods(std::vector<Lod> lods);  // em qualquer ordem; guardados do maior screenSize para o menor
    const std::vector<Lod>& Lods() const { return m_lods; }
    // Esta malha ou o nível que vale para um objeto com esse tamanho na tela.
    const Mesh& ForScreenSize(float screenSize) const;

    // Primitivas (todas centradas na origem, tamanho 1). Esfera, cilindro, cápsula e cone vêm com dois níveis de LOD
    // (metade e um quarto das divisões, abaixo de 25% e de 8% da tela).
    static std::shared_ptr<Mesh> CreateCube();
    static std::shared_ptr<Mesh> CreatePlane(float uvRepeat = 1.0f);  // plano XZ, normal +Y
    static std::shared_ptr<Mesh> CreateSphere(int segments = 48, int rings = 24);
    // Fase 18: cilindro e cápsula de pé no eixo Y com altura 2 e raio 0,5 (as medidas padrão do Collider);
    // cone com altura 1 e raio 0,5 na base (ponta para cima).
    static std::shared_ptr<Mesh> CreateCylinder(int segments = 48);
    static std::shared_ptr<Mesh> CreateCapsule(int segments = 48, int rings = 12);  // anéis por meia esfera
    static std::shared_ptr<Mesh> CreateCone(int segments = 48);

private:
    void Upload() const;

    std::vector<glm::vec3> m_positions;
    std::vector<uint32_t> m_indices;
    std::vector<SkinWeights> m_weights;
    std::shared_ptr<const Skin> m_skin;
    std::vector<Lod> m_lods;
    glm::vec3 m_boundsMin{0.0f};
    glm::vec3 m_boundsMax{0.0f};

    // Vértices completos esperando o upload (liberados depois dele).
    mutable std::vector<Vertex> m_pendingVertices;
    mutable unsigned int m_vao = 0;
    mutable unsigned int m_vbo = 0;
    mutable unsigned int m_skinVbo = 0;
    mutable unsigned int m_ibo = 0;
};

} // namespace motor
