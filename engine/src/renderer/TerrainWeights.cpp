#include "motor/renderer/TerrainWeights.h"

#include "motor/core/Log.h"
#include "motor/renderer/GpuMemory.h"
#include "renderer/GL.h"

#include <algorithm>
#include <bit>
#include <climits>

namespace motor {

TerrainWeights::TerrainWeights(int resolution) : m_resolution(std::max(resolution, 2)) {}

TerrainWeights::~TerrainWeights() {
    if (m_array) {
        UntrackGpuMemory(GpuObject::Texture, m_array);
        glDeleteTextures(1, &m_array);
    }
    if (m_index) {
        UntrackGpuMemory(GpuObject::Texture, m_index);
        glDeleteTextures(1, &m_index);
    }
}

void TerrainWeights::SetBlock(int x, int z, std::span<const uint8_t> rgba) {
    const size_t bytes = static_cast<size_t>(m_resolution) * m_resolution * 4;
    auto [it, added] = m_layers.try_emplace({x, z}, -1);
    if (added) {
        if (!m_free.empty()) {
            it->second = m_free.back();
            m_free.pop_back();
        } else {
            it->second = static_cast<int>(m_pixels.size());
            m_pixels.emplace_back();
        }
        m_indexDirty = true;
    }
    std::vector<uint8_t>& pixels = m_pixels[static_cast<size_t>(it->second)];
    pixels.assign(rgba.begin(), rgba.begin() + static_cast<std::ptrdiff_t>(std::min(rgba.size(), bytes)));
    pixels.resize(bytes, 0);
    m_dirtyLayers.push_back(it->second);
}

void TerrainWeights::RemoveBlock(int x, int z) {
    const auto it = m_layers.find({x, z});
    if (it == m_layers.end()) return;
    std::vector<uint8_t>().swap(m_pixels[static_cast<size_t>(it->second)]);
    m_free.push_back(it->second);
    m_layers.erase(it);
    m_indexDirty = true;
}

std::vector<std::pair<int, int>> TerrainWeights::BlockKeys() const {
    std::vector<std::pair<int, int>> keys;
    keys.reserve(m_layers.size());
    for (const auto& [key, layer] : m_layers) keys.push_back(key);
    return keys;
}

int TerrainWeights::LayerOf(int x, int z) const {
    const auto it = m_layers.find({x, z});
    return it == m_layers.end() ? -1 : it->second;
}

glm::ivec4 TerrainWeights::IndexRect() const {
    if (m_indexDirty) {
        if (m_layers.empty()) {
            m_rect = {0, 0, 1, 1};
        } else {
            int x0 = INT_MAX, z0 = INT_MAX, x1 = INT_MIN, z1 = INT_MIN;
            for (const auto& [key, layer] : m_layers) {
                x0 = std::min(x0, key.first);
                x1 = std::max(x1, key.first);
                z0 = std::min(z0, key.second);
                z1 = std::max(z1, key.second);
            }
            m_rect = {x0, z0, x1 - x0 + 1, z1 - z0 + 1};
        }
    }
    return m_rect;
}

unsigned int TerrainWeights::ArrayHandle() const {
    Upload();
    return m_array;
}

unsigned int TerrainWeights::IndexHandle() const {
    Upload();
    return m_index;
}

void TerrainWeights::Upload() const {
    const int needed = std::max(static_cast<int>(m_pixels.size()), 1);
    static const int maxLayers = [] {
        GLint value = 256;
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &value);
        return static_cast<int>(value);
    }();
    const int levels = static_cast<int>(std::bit_width(static_cast<unsigned>(m_resolution)));
    if (needed > m_arrayCapacity && m_arrayCapacity < maxLayers) {
        // Cresce em potências de 2 (pintar ou pôr blocos não recria a cada um); tudo sobe de novo.
        const int capacity = std::min(std::max(static_cast<int>(std::bit_ceil(static_cast<unsigned>(needed))), 16), maxLayers);
        if (m_array) {
            UntrackGpuMemory(GpuObject::Texture, m_array);
            glDeleteTextures(1, &m_array);
        }
        glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_array);
        glTextureStorage3D(m_array, levels, GL_RGBA8, m_resolution, m_resolution, capacity);
        TrackGpuMemory(GpuObject::Texture, m_array, GpuMemoryKind::Textures,
                       GpuTextureBytes(GL_RGBA8, m_resolution, m_resolution, capacity, levels));
        glTextureParameteri(m_array, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(m_array, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(m_array, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_array, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_arrayCapacity = capacity;
        m_dirtyLayers.clear();
        for (int layer = 0; layer < static_cast<int>(m_pixels.size()); ++layer) m_dirtyLayers.push_back(layer);
        m_indexDirty = true;
    }
    if (needed > m_arrayCapacity && !std::exchange(m_warnedLimit, true)) {
        log::Warn("Terreno: {} blocos, mas a GPU guarda pesos de só {}: os outros ficam com a camada 1", needed,
                  m_arrayCapacity);
    }
    if (!m_dirtyLayers.empty()) {
        std::sort(m_dirtyLayers.begin(), m_dirtyLayers.end());
        m_dirtyLayers.erase(std::unique(m_dirtyLayers.begin(), m_dirtyLayers.end()), m_dirtyLayers.end());
        for (const int layer : m_dirtyLayers) {
            const std::vector<uint8_t>& pixels = m_pixels[static_cast<size_t>(layer)];
            if (layer >= m_arrayCapacity || pixels.empty()) continue;
            glTextureSubImage3D(m_array, 0, 0, 0, layer, m_resolution, m_resolution, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                                pixels.data());
        }
        glGenerateTextureMipmap(m_array);
        m_dirtyLayers.clear();
    }
    if (m_indexDirty || !m_index) {
        const glm::ivec4 rect = IndexRect();
        std::vector<float> cells(static_cast<size_t>(rect.z) * static_cast<size_t>(rect.w), -1.0f);
        for (const auto& [key, layer] : m_layers) {
            if (layer >= m_arrayCapacity) continue;
            cells[static_cast<size_t>(key.second - rect.y) * static_cast<size_t>(rect.z) + static_cast<size_t>(key.first - rect.x)] =
                static_cast<float>(layer);
        }
        // Tamanho novo: textura nova (o armazenamento do glTextureStorage2D é fixo).
        static_assert(sizeof(float) == 4);
        if (m_index) {
            UntrackGpuMemory(GpuObject::Texture, m_index);
            glDeleteTextures(1, &m_index);
        }
        glCreateTextures(GL_TEXTURE_2D, 1, &m_index);
        glTextureStorage2D(m_index, 1, GL_R32F, rect.z, rect.w);
        TrackGpuMemory(GpuObject::Texture, m_index, GpuMemoryKind::Textures, GpuTextureBytes(GL_R32F, rect.z, rect.w));
        glTextureParameteri(m_index, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(m_index, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureSubImage2D(m_index, 0, 0, 0, rect.z, rect.w, GL_RED, GL_FLOAT, cells.data());
        m_indexDirty = false;
    }
}

} // namespace motor
