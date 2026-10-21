#pragma once

#include <motor/core/FrameStats.h>

#include <imgui.h>

#include <string>
#include <vector>

namespace editor {

// Painel dos contadores por quadro (Fase 22) por cima do viewport: duração do quadro, CPU por parte, GPU por passada,
// draws e memória de vídeo por tipo. Lê motor::FrameStats e suaviza (média móvel) para os números não piscarem.
class FrameStatsPanel {
public:
    // Desenha com o canto superior direito em `topRight` (coordenadas de tela), com no máximo `maxHeight` de altura.
    void Draw(ImDrawList* dl, const ImVec2& topRight, float maxHeight);

private:
    struct Smoothed {
        std::string name;
        double value = 0.0;
    };
    void Update(const motor::FrameStats::Frame& frame);
    static void Blend(std::vector<Smoothed>& into, const std::vector<motor::FrameStats::Entry>& from, double alpha);

    uint64_t m_lastFrame = 0;
    double m_frameMs = 0.0;
    std::vector<Smoothed> m_cpu;
    std::vector<Smoothed> m_gpu;
    std::vector<Smoothed> m_values;
};

} // namespace editor
