#pragma once

// Fase 28: pincéis do terreno (subir, baixar, suavizar, nivelar). Sem OpenGL nem editor: o viewport e o MCP usam a
// mesma pincelada, e os testes também.
//
// Uma pincelada vai do apertar ao soltar do botão. A cada quadro, Apply mexe nas amostras dentro do raio, com mais
// força no centro e nada na borda (queda em cosseno). A pincelada guarda a altura de antes de cada amostra que mexeu:
// Finish devolve as mudanças, que o desfazer põe e tira com ApplyTerrainChanges.

#include "motor/assets/TerrainData.h"

#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace motor {

enum class TerrainBrushMode { Raise, Lower, Smooth, Flatten };

struct TerrainBrush {
    TerrainBrushMode mode = TerrainBrushMode::Raise;
    float radius = 8.0f;    // metros, no espaço do terreno
    float strength = 0.5f;  // 0 a 1

    static constexpr float kMinRadius = 0.5f;
    static constexpr float kMaxRadius = 128.0f;
    static constexpr float kRaiseSpeed = 8.0f;  // metros por segundo no centro, com força 1
    static constexpr float kBlendRate = 12.0f;  // suavizar e nivelar: quanto se aproxima do alvo por segundo, força 1
};

// Uma amostra mudada por uma pincelada: posição na grade (TerrainData::Sample) e as alturas de antes e de depois.
struct TerrainSampleChange {
    int gx = 0;
    int gz = 0;
    float before = 0.0f;
    float after = 0.0f;
};

class TerrainStroke {
public:
    TerrainStroke(std::shared_ptr<TerrainData> data, const TerrainBrush& brush);

    // Pincel com o centro em (x, z) (espaço do terreno) durante `dt` segundos. Devolve quantas amostras mudaram.
    int Apply(float x, float z, float dt);

    const std::shared_ptr<TerrainData>& Data() const { return m_data; }
    const TerrainBrush& Brush() const { return m_brush; }
    // Mudanças da pincelada inteira, em ordem (z, depois x); amostras que voltaram à altura de antes ficam de fora.
    std::vector<TerrainSampleChange> Finish() const;

private:
    std::shared_ptr<TerrainData> m_data;
    TerrainBrush m_brush;
    std::map<std::pair<int, int>, float> m_before;  // chave (gz, gx): altura antes da primeira mudança
    std::optional<float> m_flattenHeight;           // nivelar: altura sob o primeiro ponto da pincelada
};

// Põe as alturas de depois (refazer) ou as de antes (desfazer).
void ApplyTerrainChanges(TerrainData& data, const std::vector<TerrainSampleChange>& changes, bool after);

} // namespace motor
