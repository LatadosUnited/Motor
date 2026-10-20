#pragma once

// Zonas de GPU do profiler (Fase 22), privadas da camada renderer: precisam das funções do nosso carregador de GL
// (renderer/GL.h). MOTOR_PROFILE_GPU marca a mesma passada na CPU e na GPU (timer queries do OpenGL).
//
//   MOTOR_PROFILE_GPU_CONTEXT();   uma vez, com o contexto já carregado (construtor do Renderer)
//   MOTOR_PROFILE_GPU_COLLECT();   uma vez por quadro: lê as consultas que a GPU já terminou
//   MOTOR_PROFILE_GPU("Sombras");  zona de CPU e de GPU até o fim do bloco

//
// Fora do Tracy, MOTOR_PROFILE_GPU também mede a passada para os contadores (GpuTimers -> FrameStats), sempre ligado.

#include "motor/core/Profile.h"
#include "renderer/GL.h"
#include "renderer/GpuTimers.h"

#if defined(MOTOR_TRACY)

#include <tracy/TracyOpenGL.hpp>

#define MOTOR_PROFILE_GPU_CONTEXT() \
    TracyGpuContext;                \
    TracyGpuContextName("OpenGL", 6)
#define MOTOR_PROFILE_GPU_COLLECT() TracyGpuCollect
#define MOTOR_PROFILE_GPU(name)                          \
    const ::motor::GpuTimerScope motorGpuTimer_(name);   \
    ZoneScopedN(name);                                   \
    TracyGpuZone(name)

#else

#define MOTOR_PROFILE_GPU_CONTEXT() ((void)0)
#define MOTOR_PROFILE_GPU_COLLECT() ((void)0)
#define MOTOR_PROFILE_GPU(name) const ::motor::GpuTimerScope motorGpuTimer_(name)

#endif
