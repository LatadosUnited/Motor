#pragma once

// Header privado do renderer (Fase 28): buffer mapeado de forma persistente para o que muda a cada quadro (objetos,
// índices das passadas, comandos de desenho). A CPU escreve direto na memória que a GPU lê, sem cópia do driver; uma
// cerca no fim de cada cena marca até onde a GPU precisa ler antes de o pedaço voltar a ser escrito (anel).

#include "motor/renderer/GpuAllocators.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

typedef struct __GLsync* GLsync;

namespace motor {

class StreamBuffer {
public:
    explicit StreamBuffer(uint64_t capacity = 8ull << 20);
    ~StreamBuffer();
    StreamBuffer(const StreamBuffer&) = delete;
    StreamBuffer& operator=(const StreamBuffer&) = delete;

    struct Slice {
        void* data = nullptr;
        uint64_t offset = 0;  // em Handle()
    };
    // Espaço para `bytes`, alinhado a `alignment` (potência de 2 até 256). Espera a GPU se o anel deu a volta e ela
    // ainda lê ali; cresce se uma cena só não cabe. Crescer troca o Handle(): quem vinculou o antigo vincula de novo.
    Slice Allocate(uint64_t bytes, uint64_t alignment);
    // Fim do uso por uma cena (Renderer::EndScene).
    void Fence();

    unsigned Handle() const { return m_buffer; }
    uint64_t Capacity() const { return m_ring.Capacity(); }

private:
    void Create(uint64_t capacity);
    void Grow(uint64_t bytes);
    void ReleaseRetired();

    struct PendingFence {
        GLsync sync = nullptr;
        uint64_t end = 0;  // posição do anel até onde a cena escreveu
    };
    struct Retired {
        unsigned buffer = 0;
        GLsync sync = nullptr;  // depois do último uso; nulo até o próximo Fence
    };

    unsigned m_buffer = 0;
    std::byte* m_data = nullptr;
    RingAllocator m_ring{0};
    uint64_t m_done = 0;     // a GPU terminou tudo antes desta posição
    uint64_t m_fenced = 0;   // posição da última cerca
    std::deque<PendingFence> m_fences;
    std::vector<Retired> m_retired;  // buffers trocados ao crescer, apagados quando a GPU passar deles
};

} // namespace motor
