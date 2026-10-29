// Fase 28: componentes e entidades em blocos contínuos por tipo (BlockPool).

#include "TestUtil.h"

#include "motor/scene/BlockPool.h"
#include "motor/scene/Components.h"
#include "motor/scene/Scene.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <thread>
#include <vector>

using namespace motor;

namespace {
struct alignas(32) Grande {
    float valores[20] = {};
};
} // namespace

int main() {
    SECTION("Pedaços seguidos, alinhados, sem repetir; o liberado volta primeiro");
    {
        BlockPool pool(sizeof(Grande), alignof(Grande));
        CHECK(pool.SlotSize() % 32 == 0 && pool.SlotSize() >= sizeof(Grande));
        std::vector<void*> slots;
        for (int i = 0; i < 2000; ++i) slots.push_back(pool.Allocate());
        bool aligned = true, sequential = true;
        for (size_t i = 0; i < slots.size(); ++i) {
            aligned &= reinterpret_cast<uintptr_t>(slots[i]) % 32 == 0;
            // Dentro de um bloco, um logo depois do outro.
            if (i > 0 && static_cast<std::byte*>(slots[i]) != static_cast<std::byte*>(slots[i - 1]) + pool.SlotSize()) {
                sequential &= i % (64 * 1024 / pool.SlotSize()) == 0;
            }
        }
        CHECK(aligned && sequential);
        CHECK(std::set<void*>(slots.begin(), slots.end()).size() == slots.size());
        CHECK(pool.Live() == 2000 && pool.Blocks() > 1);
        pool.Free(slots[10]);
        CHECK(pool.Allocate() == slots[10]);
        for (void* p : slots) pool.Free(p);
        CHECK(pool.Live() == 0);
        const size_t blocks = pool.Blocks();
        for (int i = 0; i < 2000; ++i) pool.Allocate();  // reaproveita, sem bloco novo
        CHECK(pool.Blocks() == blocks);
    }

    SECTION("Mesmo nome, tamanho e alinhamento: o mesmo pool");
    {
        BlockPool& a = BlockPool::For("TesteBloco", 48, 8);
        BlockPool& b = BlockPool::For("TesteBloco", 48, 8);
        BlockPool& c = BlockPool::For("TesteBloco", 64, 8);
        CHECK(&a == &b && &a != &c);
    }

    SECTION("Várias threads ao mesmo tempo");
    {
        BlockPool pool(24, 8);
        std::vector<std::vector<void*>> got(4);
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&, t] {
                for (int i = 0; i < 5000; ++i) {
                    got[t].push_back(pool.Allocate());
                    if (i % 3 == 0) {
                        pool.Free(got[t].back());
                        got[t].pop_back();
                    }
                }
            });
        }
        for (auto& th : threads) th.join();
        std::set<void*> all;
        size_t count = 0;
        for (const auto& v : got) {
            all.insert(v.begin(), v.end());
            count += v.size();
        }
        CHECK(all.size() == count && pool.Live() == count);
    }

    SECTION("Componentes e entidades da cena: criados em sequência, lado a lado");
    {
        Scene scene;
        std::vector<const MeshRenderer*> renderers;
        std::vector<const Entity*> entities;
        for (int i = 0; i < 300; ++i) {
            Entity& e = scene.CreateEntity("Cubo");
            renderers.push_back(&e.Add<MeshRenderer>());
            entities.push_back(&e);
        }
        auto close = [](const auto& list) {
            size_t near = 0;
            for (size_t i = 1; i < list.size(); ++i) {
                const auto* a = reinterpret_cast<const std::byte*>(list[i - 1]);
                const auto* b = reinterpret_cast<const std::byte*>(list[i]);
                near += b > a && b - a < 1024;
            }
            return near;
        };
        // Quase todos logo depois do anterior (fora as viradas de bloco e pedaços liberados por outros testes).
        CHECK(close(renderers) > 280);
        CHECK(close(entities) > 280);
        // Cópia da cena: os valores vêm junto, em pedaços novos.
        Scene copy(scene);
        CHECK(copy == scene);
        CHECK(copy.Find(entities[5]->id) != entities[5]);
        CHECK(copy.Find(entities[5]->id)->Get<MeshRenderer>() != renderers[5]);
    }
    return TestResult();
}
