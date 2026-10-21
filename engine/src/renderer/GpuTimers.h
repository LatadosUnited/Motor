#pragma once

// Tempo de GPU por passada do renderer (Fase 22), para os contadores de FrameStats. Independente do Tracy: sempre
// ligado. Cada passada marca dois carimbos de tempo (glQueryCounter, GL_TIMESTAMP), que podem se aninhar; a GPU
// responde alguns quadros depois, e a leitura só acontece quando a resposta já está pronta (a CPU nunca espera).

#include <cstdint>
#include <vector>

namespace motor {

class GpuTimers {
public:
    GpuTimers();  // MOTOR_GPU_TIMERS=0 (variável de ambiente) desliga
    ~GpuTimers();
    GpuTimers(const GpuTimers&) = delete;
    GpuTimers& operator=(const GpuTimers&) = delete;

    // Uma vez por quadro, antes das passadas: lê as respostas que chegaram e entrega a FrameStats::AddGpu.
    void BeginFrame();
    // Começa e termina uma passada. `name` precisa durar até a leitura (um literal).
    int Begin(const char* name);
    void End(int pass);

    // O do Renderer atual (as passadas usam pelo macro MOTOR_PROFILE_GPU); nulo sem renderer.
    static GpuTimers* Current();
    static void SetCurrent(GpuTimers* timers);

private:
    static constexpr int kFrames = 4;  // quadros em voo: a resposta de um quadro é lida 4 quadros depois
    struct Pass {
        const char* name = nullptr;
        unsigned int begin = 0;  // consultas GL (do pool do quadro, reaproveitadas)
        unsigned int end = 0;
    };
    struct FrameQueries {
        std::vector<unsigned int> pool;  // consultas já criadas para este quadro
        std::vector<Pass> passes;
        size_t used = 0;
    };
    unsigned int Query(FrameQueries& frame);

    FrameQueries m_frames[kFrames];
    uint64_t m_frame = 0;
    bool m_enabled = true;
};

// Passada medida na GPU até o fim do escopo.
class GpuTimerScope {
public:
    explicit GpuTimerScope(const char* name) : m_timers(GpuTimers::Current()) {
        if (m_timers) m_pass = m_timers->Begin(name);
    }
    ~GpuTimerScope() {
        if (m_timers) m_timers->End(m_pass);
    }
    GpuTimerScope(const GpuTimerScope&) = delete;
    GpuTimerScope& operator=(const GpuTimerScope&) = delete;

private:
    GpuTimers* m_timers;
    int m_pass = -1;
};

} // namespace motor
