// Sistema de tarefas (Fase 26): ParallelFor cobre cada índice uma vez só, usa outras threads, pode ser chamado de
// dentro de uma tarefa e de várias threads ao mesmo tempo; Schedule roda tudo o que entra na fila. Repetido muitas
// vezes para uma corrida aparecer aqui, e não no jogo.
#include "TestUtil.h"

#include <motor/core/JobSystem.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

using namespace motor;

int main() {
    JobSystem& jobs = JobSystem::Get();
    std::printf("threads de trabalho: %d\n", jobs.WorkerCount());

    SECTION("ParallelFor passa por cada índice uma vez");
    {
        bool ok = true;
        for (size_t count : {size_t{0}, size_t{1}, size_t{7}, size_t{64}, size_t{1000}, size_t{100003}}) {
            for (size_t minChunk : {size_t{1}, size_t{16}, size_t{1000}}) {
                std::vector<std::atomic<int>> hits(count);
                jobs.ParallelFor(count, minChunk, [&](size_t begin, size_t end) {
                    for (size_t i = begin; i < end; ++i) hits[i].fetch_add(1, std::memory_order_relaxed);
                });
                for (const auto& h : hits) ok = ok && h.load() == 1;
            }
        }
        CHECK(ok);
    }

    SECTION("pedaços respeitam o mínimo e não se sobrepõem");
    {
        std::mutex mutex;
        std::vector<std::pair<size_t, size_t>> ranges;
        jobs.ParallelFor(10000, 300, [&](size_t begin, size_t end) {
            std::lock_guard lock(mutex);
            ranges.emplace_back(begin, end);
        });
        std::sort(ranges.begin(), ranges.end());
        bool contiguous = !ranges.empty() && ranges.front().first == 0 && ranges.back().second == 10000;
        bool bigEnough = true;
        for (size_t i = 0; i < ranges.size(); ++i) {
            if (i > 0) contiguous = contiguous && ranges[i].first == ranges[i - 1].second;
            if (i + 1 < ranges.size()) bigEnough = bigEnough && ranges[i].second - ranges[i].first >= 300;
        }
        CHECK(contiguous);
        CHECK(bigEnough);
    }

    SECTION("usa mais de uma thread quando há threads de trabalho");
    if (jobs.WorkerCount() > 0) {
        std::mutex mutex;
        std::set<std::thread::id> seen;
        // Pedaços que demoram um pouco, para as outras threads acordarem e pegarem algum.
        jobs.ParallelFor(64, 1, [&](size_t, size_t) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            std::lock_guard lock(mutex);
            seen.insert(std::this_thread::get_id());
        });
        CHECK(seen.size() > 1);
    } else {
        std::printf("  (pulado: MOTOR_THREADS=0)\n");
    }

    SECTION("ParallelFor dentro de ParallelFor e de várias threads ao mesmo tempo");
    {
        std::atomic<long long> sum{0};
        auto nested = [&] {
            jobs.ParallelFor(40, 1, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    jobs.ParallelFor(500, 50, [&](size_t b, size_t e) {
                        sum.fetch_add(static_cast<long long>(e - b), std::memory_order_relaxed);
                    });
                }
            });
        };
        std::thread other(nested);
        nested();
        other.join();
        CHECK(sum.load() == 2LL * 40 * 500);
    }

    SECTION("Schedule roda todas as tarefas");
    {
        std::atomic<int> ran{0};
        constexpr int kTasks = 5000;
        for (int i = 0; i < kTasks; ++i) jobs.Schedule([&] { ran.fetch_add(1, std::memory_order_relaxed); });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (ran.load() < kTasks && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
        CHECK(ran.load() == kTasks);
    }

    SECTION("muitas rodadas seguidas (corridas)");
    {
        bool ok = true;
        std::vector<int> data(4096);
        for (int round = 0; round < 2000 && ok; ++round) {
            jobs.ParallelFor(data.size(), 64, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) data[i] = round;
            });
            for (int v : data) ok = ok && v == round;
        }
        CHECK(ok);
    }

    return TestResult();
}
