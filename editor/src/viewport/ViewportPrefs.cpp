#include "viewport/ViewportPrefs.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace editor {

void ViewportPrefs::Clamp() {
    // NaN também volta ao padrão (as comparações com NaN são falsas).
    auto clamp = [](float& v, float lo, float hi, float fallback) {
        v = v >= lo && v <= hi ? v : (v > hi ? hi : (v < lo ? lo : fallback));
    };
    clamp(snapMove, 0.01f, 100.0f, 0.5f);
    clamp(snapRotate, 0.1f, 180.0f, 15.0f);
    clamp(snapScale, 0.001f, 10.0f, 0.1f);
    clamp(gridHeight, -10000.0f, 10000.0f, 0.0f);
}

glm::vec3 SnapToGrid(const glm::vec3& before, const glm::vec3& after, float step, float height) {
    if (!(step > 0.0f)) return after;
    glm::vec3 out = after;
    const glm::vec3 base{0.0f, height, 0.0f};
    for (int i = 0; i < 3; ++i) {
        if (after[i] == before[i]) continue;
        out[i] = std::round((after[i] - base[i]) / step) * step + base[i];
    }
    return out;
}

std::shared_ptr<motor::Mesh> CreateGridMesh(int halfCount) {
    halfCount = std::max(halfCount, 1);
    const float extent = static_cast<float>(halfCount);
    std::vector<motor::Vertex> vertices;
    std::vector<uint32_t> indices;
    auto line = [&](const glm::vec3& a, const glm::vec3& b) {
        const auto first = static_cast<uint32_t>(vertices.size());
        vertices.push_back({a, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}});
        vertices.push_back({b, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}});
        indices.insert(indices.end(), {first, first + 1, first + 1});
    };
    for (int i = -halfCount; i <= halfCount; ++i) {
        const float p = static_cast<float>(i);
        line({p, 0.0f, -extent}, {p, 0.0f, extent});
        line({-extent, 0.0f, p}, {extent, 0.0f, p});
    }
    return std::make_shared<motor::Mesh>(vertices, indices);
}

glm::vec3 GridCenter(const glm::vec3& camera, float alignTo, float height) {
    if (!(alignTo > 0.0f)) alignTo = 1.0f;
    return {std::round(camera.x / alignTo) * alignTo, height, std::round(camera.z / alignTo) * alignTo};
}

} // namespace editor
