#include "motor/project/CollisionLayers.h"

#include <format>

namespace motor {

CollisionLayers::CollisionLayers() {
    names[0] = "Padrão";
    masks.fill(static_cast<uint16_t>(kAll));
}

bool CollisionLayers::Collide(int a, int b) const {
    if (a < 0 || a >= kCount || b < 0 || b >= kCount) return false;
    return (masks[static_cast<size_t>(a)] >> b) & 1u;
}

void CollisionLayers::SetCollide(int a, int b, bool collide) {
    if (a < 0 || a >= kCount || b < 0 || b >= kCount) return;
    auto set = [collide](uint16_t& mask, int bit) {
        if (collide) mask = static_cast<uint16_t>(mask | (1u << bit));
        else mask = static_cast<uint16_t>(mask & ~(1u << bit));
    };
    set(masks[static_cast<size_t>(a)], b);
    set(masks[static_cast<size_t>(b)], a);
}

std::string CollisionLayers::Label(int index) const {
    if (index < 0 || index >= kCount) return "?";
    const std::string& name = names[static_cast<size_t>(index)];
    return name.empty() ? std::format("Camada {}", index) : name;
}

int CollisionLayers::Find(std::string_view name) const {
    for (int i = 0; i < kCount; ++i) {
        if (!names[static_cast<size_t>(i)].empty() && names[static_cast<size_t>(i)] == name) return i;
    }
    return -1;
}

uint32_t CollisionLayers::Mask(std::initializer_list<std::string_view> layerNames) const {
    uint32_t mask = 0;
    for (std::string_view name : layerNames) {
        if (const int i = Find(name); i >= 0) mask |= 1u << i;
    }
    return mask;
}

} // namespace motor
