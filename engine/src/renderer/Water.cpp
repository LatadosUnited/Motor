#include "motor/renderer/Water.h"

#include "motor/renderer/Mesh.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace motor {

namespace {
// As 4 ondas: ângulo em volta da direção principal, fração do comprimento e da altura (somam 1). Iguais ao water.vert.
constexpr float kAngles[4] = {0.0f, 0.55f, -0.85f, 1.6f};
constexpr float kLengths[4] = {1.0f, 0.61f, 0.38f, 0.23f};
constexpr float kHeights[4] = {0.52f, 0.26f, 0.14f, 0.08f};
constexpr float kSteepness = 0.6f;  // quanto as cristas se apertam (0 = senoides)
constexpr float kGravity = 9.81f;
} // namespace

glm::vec3 WaterSurface::WaveOffset(const glm::vec2& position, float seconds) const {
    glm::vec3 offset(0.0f);
    if (waveHeight <= 0.0f || waveLength <= 0.0f) return offset;
    const float base = glm::radians(waveDirection);
    for (int i = 0; i < 4; ++i) {
        const glm::vec2 dir(std::cos(base + kAngles[i]), std::sin(base + kAngles[i]));
        const float k = 2.0f * glm::pi<float>() / (waveLength * kLengths[i]);
        const float a = waveHeight * kHeights[i];
        const float w = std::sqrt(kGravity * k) * waveSpeed;
        const float f = k * glm::dot(dir, position) - w * seconds;
        const float qa = std::min(kSteepness / (4.0f * k), a);  // Q * A, sem as cristas se cruzarem
        offset.x += qa * dir.x * std::cos(f);
        offset.z += qa * dir.y * std::cos(f);
        offset.y += a * std::sin(f);
    }
    return offset;
}

std::shared_ptr<Mesh> CreateWaterGrid(const glm::vec2& size, float cellSize) {
    const glm::vec2 s = glm::max(size, glm::vec2(0.01f));
    const float cell = std::max(cellSize, 0.01f);
    const int nx = std::clamp(static_cast<int>(std::ceil(s.x / cell)), 1, 256);
    const int nz = std::clamp(static_cast<int>(std::ceil(s.y / cell)), 1, 256);
    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<size_t>(nx + 1) * (nz + 1));
    for (int j = 0; j <= nz; ++j) {
        for (int i = 0; i <= nx; ++i) {
            const float x = (static_cast<float>(i) / static_cast<float>(nx) - 0.5f) * s.x;
            const float z = (static_cast<float>(j) / static_cast<float>(nz) - 0.5f) * s.y;
            vertices.push_back({{x, 0.0f, z}, {0.0f, 1.0f, 0.0f}, {x, z}});
        }
    }
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(nx) * nz * 6);
    for (int j = 0; j < nz; ++j) {
        for (int i = 0; i < nx; ++i) {
            const uint32_t v00 = static_cast<uint32_t>(j * (nx + 1) + i), v10 = v00 + 1;
            const uint32_t v01 = v00 + static_cast<uint32_t>(nx + 1), v11 = v01 + 1;
            indices.insert(indices.end(), {v00, v01, v11, v00, v11, v10});  // anti-horário visto de cima
        }
    }
    return std::make_shared<Mesh>(vertices, indices);
}

} // namespace motor
