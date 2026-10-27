#include "motor/assets/TerrainBrush.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace motor {

TerrainStroke::TerrainStroke(std::shared_ptr<TerrainData> data, const TerrainBrush& brush)
    : m_data(std::move(data)), m_brush(brush) {
    m_brush.radius = std::clamp(m_brush.radius, TerrainBrush::kMinRadius, TerrainBrush::kMaxRadius);
    m_brush.strength = std::clamp(m_brush.strength, 0.0f, 1.0f);
}

int TerrainStroke::Apply(float x, float z, float dt) {
    if (!m_data || dt <= 0.0f || m_brush.strength <= 0.0f) return 0;
    TerrainData& data = *m_data;
    if (m_brush.mode == TerrainBrushMode::Flatten && !m_flattenHeight) {
        m_flattenHeight = data.HeightAt(x, z);
        if (!m_flattenHeight) return 0;  // começou fora do terreno: ainda não há altura para nivelar
    }

    const float spacing = data.Spacing(), radius = m_brush.radius;
    const int gx0 = static_cast<int>(std::ceil((x - radius) / spacing));
    const int gx1 = static_cast<int>(std::floor((x + radius) / spacing));
    const int gz0 = static_cast<int>(std::ceil((z - radius) / spacing));
    const int gz1 = static_cast<int>(std::floor((z + radius) / spacing));
    // Subir e baixar andam em metros; suavizar e nivelar se aproximam do alvo (mesmo resultado com quadros curtos
    // ou longos: 1 - e^(-taxa * dt)).
    const float step = TerrainBrush::kRaiseSpeed * m_brush.strength * dt;
    const float blend = 1.0f - std::exp(-TerrainBrush::kBlendRate * m_brush.strength * dt);

    // Primeiro calcula tudo e depois grava: suavizar lê as vizinhas como estavam antes deste passo.
    struct Pending {
        int gx, gz;
        float height;
    };
    std::vector<Pending> pending;
    for (int gz = gz0; gz <= gz1; ++gz) {
        for (int gx = gx0; gx <= gx1; ++gx) {
            const float dx = static_cast<float>(gx) * spacing - x, dz = static_cast<float>(gz) * spacing - z;
            const float distance = std::sqrt(dx * dx + dz * dz);
            if (distance >= radius) continue;
            const auto current = data.Sample(gx, gz);
            if (!current) continue;
            const float weight = 0.5f * (1.0f + std::cos(std::numbers::pi_v<float> * distance / radius));
            float height = *current;
            switch (m_brush.mode) {
                case TerrainBrushMode::Raise: height += step * weight; break;
                case TerrainBrushMode::Lower: height -= step * weight; break;
                case TerrainBrushMode::Smooth: {
                    float sum = 0.0f;
                    int count = 0;
                    for (int oz = -1; oz <= 1; ++oz) {
                        for (int ox = -1; ox <= 1; ++ox) {
                            if (const auto h = data.Sample(gx + ox, gz + oz)) {
                                sum += *h;
                                ++count;
                            }
                        }
                    }
                    height += (sum / static_cast<float>(count) - height) * blend * weight;
                    break;
                }
                case TerrainBrushMode::Flatten: height += (*m_flattenHeight - height) * blend * weight; break;
            }
            if (height != *current) pending.push_back({gx, gz, height});
        }
    }
    for (const Pending& p : pending) {
        m_before.try_emplace({p.gz, p.gx}, *data.Sample(p.gx, p.gz));
        data.SetSample(p.gx, p.gz, p.height);
    }
    return static_cast<int>(pending.size());
}

std::vector<TerrainSampleChange> TerrainStroke::Finish() const {
    std::vector<TerrainSampleChange> changes;
    if (!m_data) return changes;
    changes.reserve(m_before.size());
    for (const auto& [key, before] : m_before) {
        const auto after = m_data->Sample(key.second, key.first);
        if (after && *after != before) changes.push_back({key.second, key.first, before, *after});
    }
    return changes;
}

void ApplyTerrainChanges(TerrainData& data, const std::vector<TerrainSampleChange>& changes, bool after) {
    for (const TerrainSampleChange& c : changes) data.SetSample(c.gx, c.gz, after ? c.after : c.before);
}

} // namespace motor
