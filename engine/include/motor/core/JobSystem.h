#pragma once

// Sistema de tarefas (Fase 26): um pool de threads só para a engine inteira, dividido entre a montagem do quadro,
// a física (a Jolt roda nele, physics/JoltJobs) e o que mais vier. Núcleos - 1 threads de trabalho: a thread que
// pede o trabalho também trabalha enquanto espera, então o quadro usa todos os núcleos sem disputar com um pool
// separado da Jolt.
//
//   JobSystem::Get().ParallelFor(itens.size(), 256, [&](size_t begin, size_t end) {
//       for (size_t i = begin; i < end; ++i) Calcula(itens[i]);
//   });
//
// Regras:
// - ParallelFor só volta quando todos os pedaços terminaram; os pedaços não podem escrever no mesmo lugar.
// - Pode ser chamado de dentro de uma tarefa (quem chama trabalha, então não trava esperando).
// - Os scripts continuam numa thread só: nada da cena que os scripts veem é mexido aqui.
//
// O pool é criado no primeiro uso e nunca destruído: threads presas no fim do processo (ou do descarregamento da
// DLL, com o loader do Windows travado) não podem ser esperadas com segurança. O sistema mata as threads paradas.

#include <cstddef>
#include <functional>

namespace motor {

class JobSystem {
public:
    using Task = std::function<void()>;
    using RangeFn = std::function<void(size_t begin, size_t end)>;

    // O pool da engine. Threads de trabalho: núcleos - 1, ou a variável MOTOR_THREADS (0 = tudo na thread que
    // pede, útil para achar um erro de corrida ou comparar).
    static JobSystem& Get();

    // Threads de trabalho (sem contar quem pede).
    int WorkerCount() const;
    // Quantas threads podem trabalhar ao mesmo tempo num ParallelFor (trabalho + quem pede).
    int Concurrency() const { return WorkerCount() + 1; }

    // Põe uma tarefa na fila e volta na hora. Sem threads de trabalho, roda já, aqui.
    void Schedule(Task task);

    // Divide [0, count) em pedaços de pelo menos `minChunk` e roda `fn` neles nas threads livres e nesta.
    // Pouco trabalho (count <= minChunk) ou sem threads de trabalho: roda direto, sem passar pela fila.
    // `fn` não pode lançar exceção (numa thread de trabalho ela encerraria o programa).
    void ParallelFor(size_t count, size_t minChunk, const RangeFn& fn);

    // Esta thread é uma das de trabalho?
    static bool IsWorkerThread();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

private:
    explicit JobSystem(int workers);
    ~JobSystem() = default;

    struct Impl;
    Impl* m_impl;
};

} // namespace motor
