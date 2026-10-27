#include "renderer/DrawList.h"

#include "renderer/GL.h"
#include "renderer/MeshPool.h"
#include "renderer/StreamBuffer.h"

#include <algorithm>
#include <cstring>

namespace motor {

namespace {
constexpr GLuint kObjectsBinding = 4;  // object.glsl

// Igual ao DrawElementsIndirectCommand do OpenGL.
struct DrawCommand {
    uint32_t count;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t baseVertex;
    uint32_t baseInstance;  // posição na lista de índices (atributo 5 por instância)
};
} // namespace

InstanceBuffers::InstanceBuffers() : m_stream(std::make_unique<StreamBuffer>()) {
    GLint alignment = 0;
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &alignment);
    m_objectAlignment = static_cast<uint64_t>(std::clamp(alignment, 4, 256));
}

InstanceBuffers::~InstanceBuffers() = default;

void InstanceBuffers::UploadObjects(std::span<const glm::mat4> objects) {
    static const glm::mat4 kEmpty[2] = {glm::mat4(1.0f), glm::mat4(1.0f)};  // buffer vazio não pode ser vinculado
    if (objects.empty()) objects = kEmpty;
    const StreamBuffer::Slice slice = m_stream->Allocate(objects.size_bytes(), m_objectAlignment);
    std::memcpy(slice.data, objects.data(), objects.size_bytes());
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, kObjectsBinding, m_stream->Handle(),
                      static_cast<GLintptr>(slice.offset), static_cast<GLsizeiptr>(objects.size_bytes()));
}

void InstanceBuffers::UploadPass(std::span<const uint32_t> indices, std::span<const DrawBatch> batches) {
    m_vaos.clear();
    if (batches.empty()) return;
    // Índices e comandos no mesmo pedaço do anel, alinhado a 4 bytes: o baseInstance conta do começo do buffer (o
    // atributo 5 lê dele desde o byte 0), e os comandos (5 inteiros) ficam alinhados a 4 como o OpenGL pede.
    const uint64_t indexBytes = indices.size_bytes();
    const uint64_t commandBytes = batches.size() * sizeof(DrawCommand);
    const StreamBuffer::Slice slice = m_stream->Allocate(indexBytes + commandBytes, sizeof(uint32_t));
    std::memcpy(slice.data, indices.data(), indexBytes);
    const uint32_t base = static_cast<uint32_t>(slice.offset / sizeof(uint32_t));
    auto* commands = reinterpret_cast<DrawCommand*>(static_cast<std::byte*>(slice.data) + indexBytes);
    m_vaos.resize(batches.size());
    for (size_t i = 0; i < batches.size(); ++i) {
        const DrawBatch& batch = batches[i];
        const Mesh::GpuDraw& gpu = batch.first->mesh->Gpu();
        commands[i] = {gpu.indexCount, batch.count, gpu.firstIndex, gpu.baseVertex, base + batch.start};
        m_vaos[i] = gpu.vao;
    }
    m_commands = slice.offset + indexBytes;
}

void InstanceBuffers::Draw(size_t first, size_t count) {
    const unsigned vao = m_vaos[first];
    if (vao == 0 || count == 0) return;  // malha vazia
    // O atributo 5 lê os índices do anel; o buffer dele muda quando o anel cresce, e o VAO pode ter sido usado fora
    // das passadas em lote (contornos): aponta sempre (é só estado do VAO).
    glVertexArrayVertexBuffer(vao, MeshPool::kInstanceBinding, m_stream->Handle(), 0, sizeof(uint32_t));
    glBindVertexArray(vao);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_stream->Handle());
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                                reinterpret_cast<const void*>(static_cast<uintptr_t>(m_commands + first * sizeof(DrawCommand))),
                                static_cast<GLsizei>(count), sizeof(DrawCommand));
}

void InstanceBuffers::Fence() { m_stream->Fence(); }

} // namespace motor
