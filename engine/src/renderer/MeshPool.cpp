#include "renderer/MeshPool.h"

#include "motor/core/Log.h"
#include "motor/renderer/GpuMemory.h"
#include "renderer/GL.h"

#include <algorithm>
#include <cstddef>

namespace motor {

namespace {
constexpr uint64_t kFirstVertices = 64 * 1024;  // 2 MB
constexpr uint64_t kFirstIndices = 256 * 1024;  // 1 MB

MeshPool* g_pool = nullptr;  // só a thread do OpenGL

// Buffer novo de `bytes`, com o conteúdo do antigo (os `used` primeiros bytes); o antigo é apagado. Draws já
// enviados que leem o antigo continuam valendo: o OpenGL só o libera quando a GPU terminar.
unsigned GrowBuffer(unsigned old, uint64_t usedBytes, uint64_t bytes) {
    unsigned buffer = 0;
    glCreateBuffers(1, &buffer);
    glNamedBufferStorage(buffer, static_cast<GLsizeiptr>(bytes), nullptr, GL_DYNAMIC_STORAGE_BIT);
    TrackGpuMemory(GpuObject::Buffer, buffer, GpuMemoryKind::Meshes, bytes);
    if (old) {
        if (usedBytes > 0) glCopyNamedBufferSubData(old, buffer, 0, 0, static_cast<GLsizeiptr>(usedBytes));
        UntrackGpuMemory(GpuObject::Buffer, old);
        glDeleteBuffers(1, &old);
    }
    return buffer;
}

uint64_t Fit(uint64_t capacity, uint64_t needed) {
    while (capacity < needed) capacity *= 2;
    return capacity;
}
} // namespace

MeshPool::MeshPool() {
    glCreateVertexArrays(1, &m_vao);
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
    Reserve(kFirstVertices, kFirstIndices);
    AddInstanceAttribute(m_vao, m_vertices);
}

MeshPool::~MeshPool() {
    UntrackGpuMemory(GpuObject::Buffer, m_vertices);
    UntrackGpuMemory(GpuObject::Buffer, m_indices);
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vertices);
    glDeleteBuffers(1, &m_indices);
}

void MeshPool::Reserve(uint64_t vertices, uint64_t indices) {
    if (vertices > m_vertexRanges.Capacity()) {
        const uint64_t capacity = Fit(std::max(m_vertexRanges.Capacity(), kFirstVertices), vertices);
        m_vertices = GrowBuffer(m_vertices, m_vertexRanges.Capacity() * sizeof(Vertex), capacity * sizeof(Vertex));
        m_vertexRanges.Grow(capacity);
        glVertexArrayVertexBuffer(m_vao, 0, m_vertices, 0, sizeof(Vertex));
    }
    if (indices > m_indexRanges.Capacity()) {
        const uint64_t capacity = Fit(std::max(m_indexRanges.Capacity(), kFirstIndices), indices);
        m_indices = GrowBuffer(m_indices, m_indexRanges.Capacity() * sizeof(uint32_t), capacity * sizeof(uint32_t));
        m_indexRanges.Grow(capacity);
        glVertexArrayElementBuffer(m_vao, m_indices);
    }
}

MeshPool::Slot MeshPool::Add(std::span<const Vertex> vertices, std::span<const uint32_t> indices) {
    if (!g_pool) g_pool = new MeshPool();
    MeshPool& pool = *g_pool;
    // Sem espaço livre do tamanho certo: cresce até caber no fim (o que estava livre lá entra junto).
    uint64_t firstVertex = pool.m_vertexRanges.Allocate(vertices.size());
    if (firstVertex == RangeAllocator::kFailed) {
        pool.Reserve(pool.m_vertexRanges.Capacity() + vertices.size(), 0);
        firstVertex = pool.m_vertexRanges.Allocate(vertices.size());
    }
    uint64_t firstIndex = pool.m_indexRanges.Allocate(indices.size());
    if (firstIndex == RangeAllocator::kFailed) {
        pool.Reserve(0, pool.m_indexRanges.Capacity() + indices.size());
        firstIndex = pool.m_indexRanges.Allocate(indices.size());
    }
    if (firstVertex == RangeAllocator::kFailed || firstIndex == RangeAllocator::kFailed) {
        log::Error("Malha grande demais para o buffer de malhas ({} vértices, {} índices)", vertices.size(),
                        indices.size());
        if (firstVertex != RangeAllocator::kFailed) pool.m_vertexRanges.Free(firstVertex, vertices.size());
        if (firstIndex != RangeAllocator::kFailed) pool.m_indexRanges.Free(firstIndex, indices.size());
        return {};
    }
    glNamedBufferSubData(pool.m_vertices, static_cast<GLintptr>(firstVertex * sizeof(Vertex)),
                         static_cast<GLsizeiptr>(vertices.size_bytes()), vertices.data());
    glNamedBufferSubData(pool.m_indices, static_cast<GLintptr>(firstIndex * sizeof(uint32_t)),
                         static_cast<GLsizeiptr>(indices.size_bytes()), indices.data());
    ++pool.m_meshes;
    return {static_cast<uint32_t>(firstVertex), static_cast<uint32_t>(vertices.size()),
            static_cast<uint32_t>(firstIndex), static_cast<uint32_t>(indices.size())};
}

void MeshPool::Remove(const Slot& slot) {
    if (!g_pool || slot.indexCount == 0) return;
    g_pool->m_vertexRanges.Free(slot.firstVertex, slot.vertexCount);
    g_pool->m_indexRanges.Free(slot.firstIndex, slot.indexCount);
    if (--g_pool->m_meshes == 0) {
        delete g_pool;
        g_pool = nullptr;
    }
}

unsigned MeshPool::VertexArray() { return g_pool ? g_pool->m_vao : 0; }

void MeshPool::AddInstanceAttribute(unsigned vao, unsigned buffer) {
    constexpr GLuint kLocation = 5;  // object.glsl
    glEnableVertexArrayAttrib(vao, kLocation);
    glVertexArrayAttribIFormat(vao, kLocation, 1, GL_UNSIGNED_INT, 0);
    glVertexArrayAttribBinding(vao, kLocation, kInstanceBinding);
    glVertexArrayBindingDivisor(vao, kInstanceBinding, 1);
    glVertexArrayVertexBuffer(vao, kInstanceBinding, buffer, 0, sizeof(uint32_t));
}

} // namespace motor
