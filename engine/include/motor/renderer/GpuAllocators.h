#pragma once

// Fase 28: as contas de onde guardar dados nos buffers da GPU, sem OpenGL (testadas sem janela). O renderer as usa
// no buffer compartilhado das malhas (RangeAllocator) e no anel de dados por quadro (RingAllocator).

#include <cstdint>
#include <map>

namespace motor {

// Intervalos livres num espaço de tamanho fixo: dá o primeiro que cabe; ao liberar, vizinhos livres se juntam.
class RangeAllocator {
public:
    static constexpr uint64_t kFailed = UINT64_MAX;

    explicit RangeAllocator(uint64_t capacity = 0);

    uint64_t Allocate(uint64_t size);  // começo do intervalo, ou kFailed se não houver um livre desse tamanho
    void Free(uint64_t offset, uint64_t size);
    void Grow(uint64_t capacity);  // o espaço novo entra livre no fim

    uint64_t Capacity() const { return m_capacity; }
    uint64_t Used() const { return m_used; }
    size_t FreeRanges() const { return m_free.size(); }

private:
    std::map<uint64_t, uint64_t> m_free;  // começo -> tamanho
    uint64_t m_capacity = 0;
    uint64_t m_used = 0;
};

// Anel: as posições só crescem, e a posição p fica no byte p % capacidade. Um pedaço nunca dá a volta (pula para o
// começo). Quem usa guarda até onde a GPU já leu (cercas) e só escreve depois que ela passou de `required`.
class RingAllocator {
public:
    struct Reservation {
        uint64_t position = 0;  // crescente; o byte no buffer é Offset(position)
        uint64_t required = 0;  // a GPU precisa ter terminado tudo antes desta posição
    };

    explicit RingAllocator(uint64_t capacity) : m_capacity(capacity) {}

    // `alignment` divide a capacidade (potências de 2 até 256).
    Reservation Reserve(uint64_t size, uint64_t alignment) const;
    void Commit(const Reservation& reservation, uint64_t size) { m_head = reservation.position + size; }

    uint64_t Head() const { return m_head; }
    uint64_t Capacity() const { return m_capacity; }
    uint64_t Offset(uint64_t position) const { return position % m_capacity; }

private:
    uint64_t m_capacity;
    uint64_t m_head = 0;
};

} // namespace motor
