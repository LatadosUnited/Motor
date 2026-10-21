#include "renderer/GpuTimers.h"

#include "motor/core/FrameStats.h"
#include "renderer/GL.h"

#include <cstdlib>

namespace motor {

namespace {
GpuTimers* g_current = nullptr;
}

GpuTimers* GpuTimers::Current() {
    return g_current;
}

void GpuTimers::SetCurrent(GpuTimers* timers) {
    g_current = timers;
}

GpuTimers::GpuTimers() {
    // MOTOR_GPU_TIMERS=0 desliga as consultas (comparar o custo delas, ou contornar um driver que se dê mal com elas).
    const wchar_t* env = _wgetenv(L"MOTOR_GPU_TIMERS");
    m_enabled = !(env && env[0] == L'0');
}

GpuTimers::~GpuTimers() {
    if (g_current == this) g_current = nullptr;
    for (FrameQueries& frame : m_frames) {
        if (!frame.pool.empty()) glDeleteQueries(static_cast<GLsizei>(frame.pool.size()), frame.pool.data());
    }
}

unsigned int GpuTimers::Query(FrameQueries& frame) {
    if (frame.used == frame.pool.size()) {
        GLuint q = 0;
        glGenQueries(1, &q);
        frame.pool.push_back(q);
    }
    return frame.pool[frame.used++];
}

void GpuTimers::BeginFrame() {
    ++m_frame;
    FrameQueries& frame = m_frames[m_frame % kFrames];  // o de kFrames quadros atrás: é a vez de ler e reusar

    // Lê o que a GPU já respondeu. A última consulta feita no quadro (o fim da passada de fora, com aninhamento) é a
    // mais tardia: as consultas terminam em ordem, então se ela está pronta, todas estão.
    if (!frame.passes.empty() && frame.used > 0) {
        GLint ready = 0;
        glGetQueryObjectiv(frame.pool[frame.used - 1], GL_QUERY_RESULT_AVAILABLE, &ready);
        for (const Pass& pass : frame.passes) {
            if (!ready) break;  // a GPU está mais de kFrames atrasada: este quadro fica sem leitura
            if (!pass.end) continue;  // passada que não terminou (exceção no meio)
            GLuint64 begin = 0, end = 0;
            glGetQueryObjectui64v(pass.begin, GL_QUERY_RESULT, &begin);
            glGetQueryObjectui64v(pass.end, GL_QUERY_RESULT, &end);
            if (end >= begin) FrameStats::Instance().AddGpu(pass.name, static_cast<double>(end - begin) / 1e6);
        }
    }
    frame.passes.clear();
    frame.used = 0;
}

int GpuTimers::Begin(const char* name) {
    if (!m_enabled) return -1;
    FrameQueries& frame = m_frames[m_frame % kFrames];
    Pass pass;
    pass.name = name;
    pass.begin = Query(frame);
    glQueryCounter(pass.begin, GL_TIMESTAMP);
    frame.passes.push_back(pass);
    return static_cast<int>(frame.passes.size() - 1);
}

void GpuTimers::End(int index) {
    FrameQueries& frame = m_frames[m_frame % kFrames];
    if (index < 0 || static_cast<size_t>(index) >= frame.passes.size()) return;
    Pass& pass = frame.passes[static_cast<size_t>(index)];
    pass.end = Query(frame);
    glQueryCounter(pass.end, GL_TIMESTAMP);
}

} // namespace motor
