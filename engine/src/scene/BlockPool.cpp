#include "motor/scene/BlockPool.h"

#include <algorithm>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <tuple>

namespace motor {

namespace {
constexpr size_t kBlockBytes = 64 * 1024;
constexpr size_t kMinPerBlock = 16;
} // namespace

BlockPool::BlockPool(size_t size, size_t alignment)
    : m_alignment(std::max(alignment, alignof(FreeSlot))) {
    const size_t slot = std::max(size, sizeof(FreeSlot));
    m_slot = (slot + m_alignment - 1) / m_alignment * m_alignment;
    m_perBlock = std::max(kMinPerBlock, kBlockBytes / m_slot);
}

void* BlockPool::Allocate() {
    std::lock_guard lock(m_mutex);
    ++m_live;
    if (m_free) {
        FreeSlot* slot = m_free;
        m_free = slot->next;
        return slot;
    }
    if (m_cursor == m_end) {
        auto* block = static_cast<std::byte*>(::operator new(m_perBlock * m_slot, std::align_val_t(m_alignment)));
        m_blocks.push_back(block);
        m_cursor = block;
        m_end = block + m_perBlock * m_slot;
    }
    void* p = m_cursor;
    m_cursor += m_slot;
    return p;
}

void BlockPool::Free(void* p) {
    if (!p) return;
    std::lock_guard lock(m_mutex);
    --m_live;
    auto* slot = static_cast<FreeSlot*>(p);
    slot->next = m_free;
    m_free = slot;
}

size_t BlockPool::Live() const {
    std::lock_guard lock(m_mutex);
    return m_live;
}

size_t BlockPool::Blocks() const {
    std::lock_guard lock(m_mutex);
    return m_blocks.size();
}

BlockPool& BlockPool::For(std::string_view name, size_t size, size_t alignment) {
    // Nunca destruídos (ver o header): os pools e o mapa ficam até o fim do processo.
    static std::mutex mutex;
    static auto* pools = new std::map<std::tuple<std::string, size_t, size_t>, std::unique_ptr<BlockPool>, std::less<>>();
    std::lock_guard lock(mutex);
    auto key = std::make_tuple(std::string(name), size, alignment);
    auto it = pools->find(key);
    if (it == pools->end()) it = pools->emplace(std::move(key), std::make_unique<BlockPool>(size, alignment)).first;
    return *it->second;
}

} // namespace motor
