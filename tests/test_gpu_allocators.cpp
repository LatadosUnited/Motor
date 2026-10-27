// Fase 28: as contas do desenho pela GPU, sem OpenGL. RangeAllocator (malhas num buffer só) e RingAllocator (anel
// de dados por quadro do buffer mapeado de forma persistente).

#include "TestUtil.h"

#include "motor/renderer/GpuAllocators.h"

#include <algorithm>
#include <random>
#include <vector>

using namespace motor;

int main() {
    SECTION("Intervalos: o primeiro que cabe, e vizinhos livres se juntam");
    {
        RangeAllocator ranges(100);
        const uint64_t a = ranges.Allocate(30);
        const uint64_t b = ranges.Allocate(30);
        const uint64_t c = ranges.Allocate(30);
        CHECK(a == 0 && b == 30 && c == 60);
        CHECK(ranges.Allocate(20) == RangeAllocator::kFailed);
        CHECK(ranges.Used() == 90);
        ranges.Free(b, 30);
        CHECK(ranges.Allocate(10) == 30);  // no buraco do meio
        ranges.Free(30, 10);
        ranges.Free(a, 30);
        ranges.Free(c, 30);
        CHECK(ranges.Used() == 0 && ranges.FreeRanges() == 1);
        CHECK(ranges.Allocate(100) == 0);
        CHECK(ranges.Allocate(0) == RangeAllocator::kFailed);
    }

    SECTION("Crescer: o espaço novo junta com o livre do fim");
    {
        RangeAllocator ranges(100);
        ranges.Allocate(80);
        ranges.Grow(200);
        CHECK(ranges.Capacity() == 200 && ranges.FreeRanges() == 1 && ranges.Used() == 80);
        CHECK(ranges.Allocate(120) == 80);
        RangeAllocator empty;
        CHECK(empty.Allocate(1) == RangeAllocator::kFailed);
        empty.Grow(16);
        CHECK(empty.Allocate(16) == 0);
    }

    SECTION("Intervalos ao acaso: nunca dois se sobrepõem, e tudo volta a ficar livre");
    {
        RangeAllocator ranges(1 << 16);
        std::mt19937 rng(3);
        std::vector<std::pair<uint64_t, uint64_t>> live;
        bool overlap = false;
        for (int step = 0; step < 4000; ++step) {
            if (!live.empty() && (rng() % 3 == 0)) {
                const size_t i = rng() % live.size();
                ranges.Free(live[i].first, live[i].second);
                live[i] = live.back();
                live.pop_back();
                continue;
            }
            const uint64_t size = 1 + rng() % 700;
            const uint64_t at = ranges.Allocate(size);
            if (at == RangeAllocator::kFailed) continue;
            for (const auto& [o, s] : live) overlap |= at < o + s && o < at + size;
            overlap |= at + size > ranges.Capacity();
            live.emplace_back(at, size);
        }
        CHECK(!overlap);
        for (const auto& [o, s] : live) ranges.Free(o, s);
        CHECK(ranges.Used() == 0 && ranges.FreeRanges() == 1);
    }

    SECTION("Anel: alinha, não dá a volta no meio de um pedaço e diz até onde a GPU precisa ter lido");
    {
        RingAllocator ring(1024);
        auto r = ring.Reserve(100, 16);
        CHECK(r.position == 0 && r.required == 0);
        ring.Commit(r, 100);
        r = ring.Reserve(10, 16);
        CHECK(r.position == 112 && ring.Offset(r.position) == 112);
        ring.Commit(r, 10);
        r = ring.Reserve(901, 4);  // de 124 não cabe até o fim: pula para o começo da segunda volta
        CHECK(r.position == 1024 && ring.Offset(r.position) == 0 && r.required == 901);
        ring.Commit(r, 901);
        r = ring.Reserve(100, 4);  // bytes 904..1003: a GPU precisa ter lido o que veio antes da posição 1004
        CHECK(r.position == 1928 && ring.Offset(r.position) == 904 && r.required == 1004);
        ring.Commit(r, 100);
        CHECK(ring.Head() == 2028);
        r = ring.Reserve(1024, 256);  // do tamanho do anel: cabe numa volta inteira
        CHECK(ring.Offset(r.position) == 0 && r.required == r.position);
    }
    return TestResult();
}
