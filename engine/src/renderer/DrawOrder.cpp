#include "motor/renderer/DrawOrder.h"

#include <algorithm>
#include <bit>
#include <functional>
#include <numeric>

namespace motor {

namespace {
// Mistura no estilo do boost::hash_combine.
void Combine(size_t& seed, size_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}

void CombineFloats(size_t& seed, const glm::vec4& v) {
    for (int i = 0; i < 4; ++i) Combine(seed, std::bit_cast<uint32_t>(v[i]));
}
} // namespace

ShadowParams ShadowParamsOf(const Material& m) {
    ShadowParams p;
    const bool cutout = m.blend == BlendMode::Cutout;
    p.cutoff = cutout ? m.alphaCutoff : -1.0f;
    if (cutout && !m.albedo) p.cutoff = m.color.a < m.alphaCutoff ? 2.0f : -1.0f;  // sem textura: tudo ou nada
    p.alpha = m.color.a;
    p.albedo = cutout ? m.albedo.get() : nullptr;
    p.backFacesOnly = m.terrain != nullptr;
    return p;
}

size_t MaterialKeyHash::operator()(const MaterialKey& key) const {
    const MaterialParams& p = key.params;
    size_t seed = key.doubleSided ? 1 : 0;
    CombineFloats(seed, p.alphaMode);
    CombineFloats(seed, p.color);
    CombineFloats(seed, p.pbr);
    CombineFloats(seed, p.emissive);
    for (const Texture2D* t : {p.albedo, p.normalMap, p.metallicRoughnessMap, p.occlusionMap, p.emissiveMap}) {
        Combine(seed, std::hash<const void*>{}(t));
    }
    Combine(seed, std::hash<const void*>{}(p.terrain));
    return seed;
}

size_t ShadowParamsHash::operator()(const ShadowParams& p) const {
    size_t seed = std::bit_cast<uint32_t>(p.cutoff);
    Combine(seed, std::bit_cast<uint32_t>(p.alpha));
    Combine(seed, std::hash<const void*>{}(p.albedo));
    Combine(seed, p.backFacesOnly ? 1 : 0);
    return seed;
}

void SortByKey(std::span<const DrawKey> keys, std::vector<uint32_t>& order) {
    order.resize(keys.size());
    std::iota(order.begin(), order.end(), 0u);
    auto keyLess = [](const DrawKey& x, const DrawKey& y) {
        if (x.shader != y.shader) return x.shader < y.shader;
        if (x.material != y.material) return x.material < y.material;
        return std::less<const void*>{}(x.mesh, y.mesh);
    };
    // Já em ordem (tudo igual, ou a cena montada assim): nada a fazer.
    if (std::is_sorted(keys.begin(), keys.end(), keyLess)) return;
    // O índice no fim da comparação mantém a ordem original entre iguais (e o resultado igual a cada quadro).
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        if (keyLess(keys[a], keys[b])) return true;
        if (keyLess(keys[b], keys[a])) return false;
        return a < b;
    });
}

} // namespace motor
