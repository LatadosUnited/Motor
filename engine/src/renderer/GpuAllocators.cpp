#include "motor/renderer/GpuAllocators.h"

#include <iterator>

namespace motor {

RangeAllocator::RangeAllocator(uint64_t capacity) : m_capacity(capacity) {
    if (capacity > 0) m_free[0] = capacity;
}

uint64_t RangeAllocator::Allocate(uint64_t size) {
    if (size == 0) return kFailed;
    for (auto it = m_free.begin(); it != m_free.end(); ++it) {
        if (it->second < size) continue;
        const uint64_t offset = it->first;
        const uint64_t rest = it->second - size;
        m_free.erase(it);
        if (rest > 0) m_free[offset + size] = rest;
        m_used += size;
        return offset;
    }
    return kFailed;
}

void RangeAllocator::Free(uint64_t offset, uint64_t size) {
    if (size == 0) return;
    m_used -= size;
    auto next = m_free.lower_bound(offset);
    if (next != m_free.begin()) {
        auto prev = std::prev(next);
        if (prev->first + prev->second == offset) {
            offset = prev->first;
            size += prev->second;
            m_free.erase(prev);
        }
    }
    if (next != m_free.end() && offset + size == next->first) {
        size += next->second;
        m_free.erase(next);
    }
    m_free[offset] = size;
}

void RangeAllocator::Grow(uint64_t capacity) {
    if (capacity <= m_capacity) return;
    const uint64_t added = capacity - m_capacity;
    const uint64_t start = m_capacity;
    m_capacity = capacity;
    m_used += added;  // o Free desconta
    Free(start, added);
}

RingAllocator::Reservation RingAllocator::Reserve(uint64_t size, uint64_t alignment) const {
    uint64_t position = (m_head + alignment - 1) / alignment * alignment;
    if (Offset(position) + size > m_capacity) position += m_capacity - Offset(position);  // não dá a volta: pula
    return {position, position + size > m_capacity ? position + size - m_capacity : 0};
}

} // namespace motor
