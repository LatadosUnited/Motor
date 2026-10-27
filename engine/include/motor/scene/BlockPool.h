#pragma once

// Fase 28: memória em blocos contínuos para objetos de um tamanho só (componentes de um tipo, entidades). Criados em
// sequência ficam lado a lado, na ordem de criação: percorrer a cena lê memória seguida em vez de saltar pelo heap
// (cada `new` caía num lugar). Um pedaço liberado volta a ser usado primeiro (lista de livres).
//
// Os blocos nunca voltam ao sistema: o pool vive até o fim do processo (componentes ainda vivos na saída, de cenas
// estáticas, não podem achar a memória já liberada). Pode ser usado de várias threads.

#include <cstddef>
#include <mutex>
#include <string_view>
#include <vector>

namespace motor {

class BlockPool {
public:
    BlockPool(size_t size, size_t alignment);
    BlockPool(const BlockPool&) = delete;
    BlockPool& operator=(const BlockPool&) = delete;

    void* Allocate();
    void Free(void* p);

    size_t SlotSize() const { return m_slot; }
    size_t Live() const;    // pedaços em uso
    size_t Blocks() const;

    // Pool de um tipo (componente pelo nome registrado, "Entity"...), criado na primeira vez: o mesmo nome com o mesmo
    // tamanho e alinhamento dá sempre o mesmo pool (scripts recarregados reaproveitam o deles).
    static BlockPool& For(std::string_view name, size_t size, size_t alignment);

private:
    struct FreeSlot {
        FreeSlot* next;
    };

    mutable std::mutex m_mutex;
    size_t m_slot;
    size_t m_alignment;
    size_t m_perBlock;
    FreeSlot* m_free = nullptr;
    std::byte* m_cursor = nullptr;  // próximo pedaço nunca usado do bloco atual
    std::byte* m_end = nullptr;
    std::vector<std::byte*> m_blocks;
    size_t m_live = 0;
};

} // namespace motor
