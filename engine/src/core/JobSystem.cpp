#include "motor/core/JobSystem.h"

#include "motor/core/Log.h"
#include "motor/core/Profile.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace motor {

namespace {
thread_local bool t_isWorker = false;

// MOTOR_THREADS: número de threads de trabalho; ausente ou inválido = núcleos - 1.
int WorkersFromEnvironment() {
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    int workers = static_cast<int>(hardware) - 1;
#if defined(_WIN32)
    const wchar_t* value = _wgetenv(L"MOTOR_THREADS");
    if (value && *value) {
        wchar_t* end = nullptr;
        const long n = std::wcstol(value, &end, 10);
        if (end && *end == L'\0' && n >= 0) workers = static_cast<int>(n);
    }
#else
    const char* value = std::getenv("MOTOR_THREADS");
    if (value && *value) {
        char* end = nullptr;
        const long n = std::strtol(value, &end, 10);
        if (end && *end == '\0' && n >= 0) workers = static_cast<int>(n);
    }
#endif
    return std::clamp(workers, 0, 63);
}
} // namespace

struct JobSystem::Impl {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Task> queue;
    std::vector<std::thread> threads;

    void WorkerMain(int index) {
        t_isWorker = true;
        const std::string name = "Tarefas " + std::to_string(index + 1);
        MOTOR_PROFILE_THREAD(name.c_str());
        (void)name;
        for (;;) {
            Task task;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return !queue.empty(); });
                task = std::move(queue.front());
                queue.pop_front();
            }
            task();
        }
    }
};

JobSystem& JobSystem::Get() {
    // Criado uma vez e nunca destruído (ver o header).
    static JobSystem* instance = new JobSystem(WorkersFromEnvironment());
    return *instance;
}

JobSystem::JobSystem(int workers) : m_impl(new Impl) {
    m_impl->threads.reserve(static_cast<size_t>(workers));
    for (int i = 0; i < workers; ++i) {
        m_impl->threads.emplace_back([impl = m_impl, i] { impl->WorkerMain(i); });
        m_impl->threads.back().detach();
    }
    log::Info("Sistema de tarefas: {} threads de trabalho", workers);
}

int JobSystem::WorkerCount() const { return static_cast<int>(m_impl->threads.size()); }

bool JobSystem::IsWorkerThread() { return t_isWorker; }

void JobSystem::Schedule(Task task) {
    if (m_impl->threads.empty()) {
        task();
        return;
    }
    {
        std::lock_guard lock(m_impl->mutex);
        m_impl->queue.push_back(std::move(task));
    }
    m_impl->wake.notify_one();
}

void JobSystem::ParallelFor(size_t count, size_t minChunk, const RangeFn& fn) {
    if (count == 0) return;
    minChunk = std::max<size_t>(minChunk, 1);
    const size_t workers = m_impl->threads.size();
    if (workers == 0 || count <= minChunk) {
        fn(0, count);
        return;
    }
    // Uns 4 pedaços por thread: quem termina antes pega outro, sem que um pedaço lento segure o resto.
    const size_t maxChunks = (workers + 1) * 4;
    const size_t chunks = std::min((count + minChunk - 1) / minChunk, maxChunks);
    const size_t chunkSize = (count + chunks - 1) / chunks;

    // O estado é dividido com as tarefas auxiliares: uma delas pode sair da fila depois que este ParallelFor voltou
    // (os pedaços já acabaram) e só precisa ver que não sobrou nada. `fn` só é lido por quem pegou um pedaço, e
    // esses terminam antes de voltarmos.
    struct State {
        const RangeFn* fn;
        size_t count, chunks, chunkSize;
        std::atomic<size_t> next{0};
        std::atomic<size_t> done{0};
        void Work() {
            for (size_t c = next.fetch_add(1, std::memory_order_relaxed); c < chunks;
                 c = next.fetch_add(1, std::memory_order_relaxed)) {
                const size_t begin = c * chunkSize;
                (*fn)(begin, std::min(begin + chunkSize, count));
                done.fetch_add(1, std::memory_order_release);
            }
        }
    };
    auto state = std::make_shared<State>();
    state->fn = &fn;
    state->count = count;
    state->chunks = chunks;
    state->chunkSize = chunkSize;

    const size_t helpers = std::min(workers, chunks - 1);
    {
        std::lock_guard lock(m_impl->mutex);
        for (size_t i = 0; i < helpers; ++i) m_impl->queue.emplace_back([state] { state->Work(); });
    }
    if (helpers == 1) m_impl->wake.notify_one();
    else m_impl->wake.notify_all();

    state->Work();
    // Sobram só pedaços já em andamento noutras threads: esperar sem pegar outra tarefa da fila (que poderia ser
    // longa, como um passo da física) mantém a espera curta.
    while (state->done.load(std::memory_order_acquire) < chunks) std::this_thread::yield();
}

} // namespace motor
