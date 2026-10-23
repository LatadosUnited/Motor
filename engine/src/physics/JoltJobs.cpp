#include "JoltJobs.h"

#include "motor/core/JobSystem.h"

#include <thread>

namespace motor::physics_jolt {

EngineJobSystem::EngineJobSystem(JPH::uint maxBarriers) { Init(maxBarriers); }

EngineJobSystem::~EngineJobSystem() {
    // A barreira volta quando as tarefas terminaram, mas a fila ainda pode ter a entrada de uma tarefa que quem
    // esperava rodou: ela solta a referência depois e chama FreeJob deste objeto. Esperamos todas saírem da fila.
    while (m_queued.load(std::memory_order_acquire) > 0) std::this_thread::yield();
}

int EngineJobSystem::GetMaxConcurrency() const { return motor::JobSystem::Get().Concurrency(); }

JPH::JobSystem::JobHandle EngineJobSystem::CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function,
                                                     JPH::uint32 dependencies) {
    // Algumas centenas de tarefas por passo: alocar cada uma custa pouco perto do passo.
    Job* job = new Job(name, color, this, function, dependencies);
    JobHandle handle(job);  // segura a tarefa: ela pode terminar logo depois de entrar na fila
    if (dependencies == 0) QueueJob(job);
    return handle;
}

void EngineJobSystem::QueueJob(Job* job) {
    // A fila guarda uma referência até a tarefa rodar. Execute() pode ser chamado também por quem espera numa
    // barreira; a Jolt garante que a tarefa roda uma vez só.
    job->AddRef();
    m_queued.fetch_add(1, std::memory_order_relaxed);
    motor::JobSystem::Get().Schedule([this, job] {
        job->Execute();
        job->Release();
        m_queued.fetch_sub(1, std::memory_order_release);
    });
}

void EngineJobSystem::QueueJobs(Job** jobs, JPH::uint count) {
    for (JPH::uint i = 0; i < count; ++i) QueueJob(jobs[i]);
}

void EngineJobSystem::FreeJob(Job* job) { delete job; }

} // namespace motor::physics_jolt
