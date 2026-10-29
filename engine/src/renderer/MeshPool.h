#pragma once

// Header privado do renderer (Fase 28): vértices e índices das malhas sem pele num buffer só, com um VAO comum,
// para um glMultiDrawElementsIndirect desenhar malhas diferentes de uma vez. Cada malha ocupa um intervalo
// (primeiro vértice e primeiro índice); os índices dela continuam contados do zero (baseVertex no comando).
//
// Atributos do VAO: 0 posição, 1 normal, 2 uv (binding 0, os vértices) e 5 o índice do objeto (binding 2, um por
// instância: InstanceBuffers aponta para a lista da passada). As malhas com pele têm VAO próprio com o mesmo
// atributo 5 (MeshPool::AddInstanceAttribute).

#include "motor/renderer/GpuAllocators.h"
#include "motor/renderer/Mesh.h"

#include <cstdint>
#include <span>

namespace motor {

class MeshPool {
public:
    static constexpr unsigned kInstanceBinding = 2;  // binding do VAO com o índice do objeto (location 5)

    struct Slot {
        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
    };

    // O pool existe enquanto houver alguma malha nele (o primeiro Add cria, o último Remove apaga os buffers).
    static Slot Add(std::span<const Vertex> vertices, std::span<const uint32_t> indices);
    static void Remove(const Slot& slot);
    static unsigned VertexArray();  // 0 se vazio

    // Atributo 5 (uint, um por instância) no binding 2 de um VAO. Até o InstanceBuffers apontar para a lista de uma
    // passada, fica em `buffer` (qualquer buffer válido: só os shaders das passadas em lote leem o atributo).
    static void AddInstanceAttribute(unsigned vao, unsigned buffer);

private:
    MeshPool();
    ~MeshPool();
    void Reserve(uint64_t vertices, uint64_t indices);

    unsigned m_vao = 0;
    unsigned m_vertices = 0;
    unsigned m_indices = 0;
    RangeAllocator m_vertexRanges;
    RangeAllocator m_indexRanges;
    uint64_t m_meshes = 0;
};

} // namespace motor
