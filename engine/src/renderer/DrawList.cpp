#include "renderer/DrawList.h"

#include "motor/renderer/GpuMemory.h"
#include "renderer/GL.h"

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

void InstanceBuffers::UploadObjects(std::span<const glm::mat4> objects) {
    static const glm::mat4 kEmpty[2] = {glm::mat4(1.0f), glm::mat4(1.0f)};  // buffer vazio não pode ser vinculado
    if (objects.empty()) objects = kEmpty;
    Upload(m_objects, m_objectBytes, objects.data(), objects.size_bytes());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kObjectsBinding, m_objects);
}

void InstanceBuffers::UploadIndices(std::span<const uint32_t> indices) {
    static const uint32_t kEmpty = 0;  // buffer vazio não pode ser vinculado
    Upload(m_indices, m_indexBytes, indices.empty() ? &kEmpty : indices.data(),
           std::max<size_t>(indices.size(), 1) * sizeof(uint32_t));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kIndicesBinding, m_indices);
}

} // namespace motor
