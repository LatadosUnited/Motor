#pragma once

// Header privado da física: as tarefas da Jolt no sistema de tarefas da engine (Fase 26), em vez de um pool de
// threads só dela. As barreiras (esperar um grupo de tarefas) vêm prontas do JobSystemWithBarrier da Jolt; aqui
// só criamos as tarefas e as pomos na fila do motor::JobSystem.

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemWithBarrier.h>

#include <atomic>

namespace motor::physics_jolt {

class EngineJobSystem final : public JPH::JobSystemWithBarrier {
public:
    explicit EngineJobSystem(JPH::uint maxBarriers);
    ~EngineJobSystem() override;

    int GetMaxConcurrency() const override;
    JobHandle CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function,
                        JPH::uint32 dependencies = 0) override;

protected:
    void QueueJob(Job* job) override;
    void QueueJobs(Job** jobs, JPH::uint count) override;
    void FreeJob(Job* job) override;

private:
    std::atomic<int> m_queued{0};  // entradas na fila do motor::JobSystem que ainda vão tocar nas tarefas
};

} // namespace motor::physics_jolt
