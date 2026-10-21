#include "renderer/DrawList.h"

#include "motor/renderer/GpuMemory.h"
#include "renderer/GL.h"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>

namespace motor {

namespace {
constexpr GLuint kObjectsBinding = 4;  // object.glsl
constexpr GLuint kIndicesBinding = 5;

// Grava `bytes` num buffer novo a cada vez (glNamedBufferData: o driver dá outra memória se a GPU ainda estiver
// lendo a anterior, sem esperar), e o registro de memória acompanha o maior tamanho.
void Upload(GLuint buffer, size_t& tracked, const void* data, size_t bytes) {
    glNamedBufferData(buffer, static_cast<GLsizeiptr>(bytes), data, GL_STREAM_DRAW);
    if (bytes > tracked) {
        tracked = bytes;
        TrackGpuMemory(GpuObject::Buffer, buffer, GpuMemoryKind::Buffers, bytes);
    }
}
} // namespace

InstanceBuffers::InstanceBuffers() {
    glCreateBuffers(1, &m_objects);
    glCreateBuffers(1, &m_indices);
}

InstanceBuffers::~InstanceBuffers() {
    UntrackGpuMemory(GpuObject::Buffer, m_objects);
    UntrackGpuMemory(GpuObject::Buffer, m_indices);
    glDeleteBuffers(1, &m_objects);
    glDeleteBuffers(1, &m_indices);
}

void InstanceBuffers::UploadObjects(std::span<const DrawItem> items) {
    // Object do object.glsl: model e a inversa transposta da parte 3x3 (colunas de 4 floats, std430).
    m_scratch.resize(std::max<size_t>(items.size(), 1) * 2);
    for (size_t i = 0; i < items.size(); ++i) {
        m_scratch[i * 2] = items[i].model;
        m_scratch[i * 2 + 1] = glm::mat4(glm::inverseTranspose(glm::mat3(items[i].model)));
    }
    Upload(m_objects, m_objectBytes, m_scratch.data(), m_scratch.size() * sizeof(glm::mat4));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kObjectsBinding, m_objects);
}

void InstanceBuffers::UploadIndices(std::span<const uint32_t> indices) {
    static const uint32_t kEmpty = 0;  // buffer vazio não pode ser vinculado
    Upload(m_indices, m_indexBytes, indices.empty() ? &kEmpty : indices.data(),
           std::max<size_t>(indices.size(), 1) * sizeof(uint32_t));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kIndicesBinding, m_indices);
}

} // namespace motor
