#pragma once

// Marcadores do profiler (Fase 22). Com o build -DMOTOR_TRACY=ON viram zonas do Tracy; sem ele, não geram código.
// O resto do Motor usa só estes macros, nunca o Tracy direto: assim o profiler pode ser trocado sem mexer no código.
//
//   MOTOR_PROFILE_FRAME();                  fim de um quadro (uma vez por volta do loop principal)
//   MOTOR_PROFILE_SCOPE("Sombras");         zona até o fim do bloco; o nome tem de ser um literal
//   MOTOR_PROFILE_FUNCTION();               zona com o nome da função
//   MOTOR_PROFILE_SCOPE_NAMED(nome);        zona com nome decidido em tempo de execução (std::string_view)
//   MOTOR_PROFILE_PLOT("Draws", valor);     série numérica por quadro no gráfico do Tracy
//   MOTOR_PROFILE_THREAD("Física 1");       nome da thread atual na linha do tempo
//
// Zonas de GPU (passadas do renderer): `renderer/GpuProfile.h`, privado da camada renderer.
// Não inclua este header em headers públicos lidos pela DLL de scripts: ela é compilada sem MOTOR_TRACY.

#if defined(MOTOR_TRACY)

#include <tracy/Tracy.hpp>

#include <string_view>

#define MOTOR_PROFILE_FRAME() FrameMark
#define MOTOR_PROFILE_SCOPE(name) ZoneScopedN(name)
#define MOTOR_PROFILE_FUNCTION() ZoneScoped
#define MOTOR_PROFILE_SCOPE_NAMED(name)                                         \
    ZoneScoped;                                                                 \
    do {                                                                        \
        const std::string_view motorProfileName_ = (name);                      \
        ZoneName(motorProfileName_.data(), motorProfileName_.size());           \
    } while (0)
#define MOTOR_PROFILE_PLOT(name, value) TracyPlot(name, static_cast<int64_t>(value))
#define MOTOR_PROFILE_THREAD(name) tracy::SetThreadName(name)

#else

#define MOTOR_PROFILE_FRAME() ((void)0)
#define MOTOR_PROFILE_SCOPE(name) ((void)0)
#define MOTOR_PROFILE_FUNCTION() ((void)0)
#define MOTOR_PROFILE_SCOPE_NAMED(name) ((void)0)
#define MOTOR_PROFILE_PLOT(name, value) ((void)0)
#define MOTOR_PROFILE_THREAD(name) ((void)0)

#endif
