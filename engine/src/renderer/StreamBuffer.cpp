#include "renderer/StreamBuffer.h"

#include "motor/renderer/GpuMemory.h"
#include "renderer/GL.h"

#include <algorithm>

namespace motor {

namespace {
constexpr GLbitfield kFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;

// Espera a GPU passar da cerca (com flush na primeira vez, senão ela pode nem ter sido enviada).
void Wait(GLsync sync) {
    GLbitfield flags = GL_SYNC_FLUSH_COMMANDS_BIT;
    for (;;) {
        const GLenum result = glClientWaitSync(sync, flags, 1'000'000'000);
        if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED || result == GL_WAIT_FAILED) return;
        flags = 0;
    }
}

bool Signaled(GLsync sync) {
    const GLenum result = glClientWaitSync(sync, 0, 0);
    return result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED || result == GL_WAIT_FAILED;
}
} // namespace

StreamBuffer::StreamBuffer(uint64_t capacity) { Create(capacity); }

StreamBuffer::~StreamBuffer() {
    for (const PendingFence& f : m_fences) glDeleteSync(f.sync);
    for (const Retired& r : m_retired) {
        if (r.sync) glDeleteSync(r.sync);
        glDeleteBuffers(1, &r.buffer);
    }
    UntrackGpuMemory(GpuObject::Buffer, m_buffer);
    glDeleteBuffers(1, &m_buffer);  // apagar desfaz o mapeamento
}

void StreamBuffer::Create(uint64_t capacity) {
    capacity = (capacity + 255) / 256 * 256;  // os alinhamentos dividem a capacidade
    glCreateBuffers(1, &m_buffer);
    glNamedBufferStorage(m_buffer, static_cast<GLsizeiptr>(capacity), nullptr, kFlags);
    m_data = static_cast<std::byte*>(glMapNamedBufferRange(m_buffer, 0, static_cast<GLsizeiptr>(capacity), kFlags));
    TrackGpuMemory(GpuObject::Buffer, m_buffer, GpuMemoryKind::Buffers, capacity);
    m_ring = RingAllocator(capacity);
    m_done = 0;
    m_fenced = 0;
}

void StreamBuffer::Grow(uint64_t bytes) {
    // O antigo pode estar vinculado e sendo lido pelo que já foi enviado: só sai quando a GPU passar dele.
    for (const PendingFence& f : m_fences) glDeleteSync(f.sync);
    m_fences.clear();
    UntrackGpuMemory(GpuObject::Buffer, m_buffer);
    glUnmapNamedBuffer(m_buffer);
    m_retired.push_back({m_buffer, nullptr});
    Create(std::max(2 * m_ring.Capacity(), 4 * bytes));
}

StreamBuffer::Slice StreamBuffer::Allocate(uint64_t bytes, uint64_t alignment) {
    if (bytes == 0) bytes = alignment;
    if (2 * bytes > m_ring.Capacity()) Grow(bytes);
    RingAllocator::Reservation r = m_ring.Reserve(bytes, alignment);
    while (m_done < r.required) {
        if (m_fences.empty()) {
            // O que está no caminho é desta cena (sem cerca ainda): ela não cabe no anel.
            Grow(bytes);
            r = m_ring.Reserve(bytes, alignment);
            continue;
        }
        Wait(m_fences.front().sync);
        glDeleteSync(m_fences.front().sync);
        m_done = m_fences.front().end;
        m_fences.pop_front();
    }
    m_ring.Commit(r, bytes);
    return {m_data + m_ring.Offset(r.position), m_ring.Offset(r.position)};
}

void StreamBuffer::Fence() {
    if (m_ring.Head() != m_fenced) {
        m_fences.push_back({glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0), m_ring.Head()});
        m_fenced = m_ring.Head();
    }
    for (Retired& r : m_retired) {
        if (!r.sync) r.sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
    ReleaseRetired();
    // Cercas já passadas: libera sem esperar (o anel não precisa delas, mas não acumulam).
    while (!m_fences.empty() && Signaled(m_fences.front().sync)) {
        glDeleteSync(m_fences.front().sync);
        m_done = m_fences.front().end;
        m_fences.pop_front();
    }
}

void StreamBuffer::ReleaseRetired() {
    std::erase_if(m_retired, [](Retired& r) {
        if (!r.sync || !Signaled(r.sync)) return false;
        glDeleteSync(r.sync);
        glDeleteBuffers(1, &r.buffer);
        return true;
    });
}

} // namespace motor
