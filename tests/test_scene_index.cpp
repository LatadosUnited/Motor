// Índices da cena (Fase 25, Tarefas 8 a 10): Children, FindByName e EntitiesWith acompanham as mudanças sem percorrer
// a cena. Confere cada um contra a varredura da cena inteira: casos diretos (ordem, SetParent, Duplicate, renomear,
// cena copiada e movida, MarkSceneStructureChanged) e uma sequência longa de mudanças ao acaso.
#include "TestUtil.h"

#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>

#include <random>
#include <string>
#include <vector>

using namespace motor;

namespace {

std::vector<EntityId> ScanChildren(const Scene& scene, EntityId parent) {
    std::vector<EntityId> ids;
    for (const auto& e : scene.Entities()) {
        if (e->parent == parent) ids.push_back(e->id);
    }
    return ids;
}

const Entity* ScanName(const Scene& scene, std::string_view name) {
    for (const auto& e : scene.Entities()) {
        if (e->name == name) return e.get();
    }
    return nullptr;
}

template <class T> std::vector<EntityId> ScanType(const Scene& scene) {
    std::vector<EntityId> ids;
    for (const auto& e : scene.Entities()) {
        if (e->Has<T>()) ids.push_back(e->id);
    }
    return ids;
}

// Todos os índices iguais à varredura: filhos de cada entidade e das raízes, os nomes dados e dois tipos.
bool Consistent(Scene& scene, const std::vector<std::string>& names) {
    if (scene.Children(kNullEntity) != ScanChildren(scene, kNullEntity)) return std::printf("  raízes\n"), false;
    for (const auto& e : scene.Entities()) {
        if (scene.Children(e->id) != ScanChildren(scene, e->id)) return std::printf("  filhos de %u\n", e->id), false;
    }
    // Com nomes repetidos e renomeações diretas, FindByName pode devolver outra de mesmo nome (ver Scene.h): confere
    // que acha uma com o nome sempre que existe alguma.
    for (const std::string& name : names) {
        const Entity* found = scene.FindByName(name);
        const bool right = ScanName(scene, name) ? found && found->name == name : !found;
        if (!right) return std::printf("  nome %s\n", name.c_str()), false;
    }
    if (scene.EntitiesWith<LightComponent>() != ScanType<LightComponent>(scene)) return std::printf("  luzes\n"), false;
    if (scene.EntitiesWith<MeshRenderer>() != ScanType<MeshRenderer>(scene)) return std::printf("  malhas\n"), false;
    return true;
}

} // namespace

int main() {
    SECTION("filhos na ordem da cena, com SetParent e destruir");
    {
        Scene scene;
        const EntityId a = scene.CreateEntity("A").id;
        const EntityId b = scene.CreateEntity("B").id;
        const EntityId a1 = scene.CreateEntity("A1", a).id;
        const EntityId a2 = scene.CreateEntity("A2", a).id;
        CHECK((scene.Children(kNullEntity) == std::vector<EntityId>{a, b}));
        CHECK((scene.Children(a) == std::vector<EntityId>{a1, a2}));

        const EntityId b1 = scene.CreateEntity("B1", b).id;
        CHECK(scene.SetParent(a1, b));
        CHECK((scene.Children(a) == std::vector<EntityId>{a2}));
        CHECK((scene.Children(b) == std::vector<EntityId>{a1, b1}));  // ordem da cena, não a da mudança

        scene.DestroyEntity(b);
        CHECK((scene.Children(kNullEntity) == std::vector<EntityId>{a}));
        CHECK(scene.Children(b).empty());
        CHECK(!scene.SetParent(a2, a2));  // ciclo: não muda nada
        CHECK((scene.Children(a) == std::vector<EntityId>{a2}));
    }

    SECTION("nomes: a primeira na ordem da cena, renomear direto e Duplicate");
    {
        Scene scene;
        Entity& x1 = scene.CreateEntity("Inimigo");
        scene.CreateEntity("Jogador");
        Entity& x2 = scene.CreateEntity("Inimigo");
        CHECK(scene.FindByName("Inimigo") == &x1);
        CHECK(scene.FindByName("Ninguém") == nullptr);

        x1.name = "Chefe";  // campo livre, sem aviso
        CHECK(scene.FindByName("Inimigo") == &x2);
        CHECK(scene.FindByName("Chefe") == &x1);
        CHECK(scene.FindByName("Chefe") == &x1);  // agora pelo índice

        const EntityId copy = scene.Duplicate(x2.id);
        CHECK(scene.FindByName("Inimigo (cópia)") == scene.Find(copy));
        scene.DestroyEntity(x2.id);
        CHECK(scene.FindByName("Inimigo") == nullptr);
    }

    SECTION("as listas mudam só na próxima consulta");
    {
        Scene scene;
        scene.CreateEntity("Luz").Add<LightComponent>();
        const std::vector<EntityId>& lights = scene.EntitiesWith<LightComponent>();
        CHECK(lights.size() == 1);
        scene.CreateEntity("Outra").Add<LightComponent>();
        CHECK(lights.size() == 1);  // quem percorre a lista não a vê mudar no meio
        CHECK(scene.EntitiesWith<LightComponent>().size() == 2);
    }

    SECTION("cena copiada e movida: Add e Remove avisam a cena certa");
    {
        Scene original;
        const EntityId id = original.CreateEntity("E").id;
        CHECK(original.EntitiesWith<LightComponent>().empty());

        Scene copia = original;
        copia.Find(id)->Add<LightComponent>();
        CHECK(copia.EntitiesWith<LightComponent>().size() == 1);
        CHECK(original.EntitiesWith<LightComponent>().empty());

        Scene movida = std::move(copia);
        movida.Find(id)->Remove<LightComponent>();
        CHECK(movida.EntitiesWith<LightComponent>().empty());
        movida.Find(id)->Add<MeshRenderer>();
        CHECK(movida.EntitiesWith<MeshRenderer>().size() == 1);

        Scene destino;
        destino.CreateEntity("Antiga");
        CHECK(destino.FindByName("Antiga") != nullptr);
        destino = std::move(movida);
        CHECK(destino.FindByName("Antiga") == nullptr);
        destino.Find(id)->Add<LightComponent>();
        CHECK(destino.EntitiesWith<LightComponent>().size() == 1);
    }

    SECTION("mudança direta em Entity::components com MarkSceneStructureChanged");
    {
        Scene scene;
        Entity& e = scene.CreateEntity("E");
        CHECK(scene.EntitiesWith<LightComponent>().empty());
        e.components.emplace_back(*ComponentRegistry::TypeOf<LightComponent>());
        MarkSceneStructureChanged();
        CHECK(scene.EntitiesWith<LightComponent>().size() == 1);
    }

    SECTION("3000 mudanças ao acaso, conferidas com a varredura");
    {
        std::mt19937 rng(25);
        const std::vector<std::string> names = {"A", "B", "C", "D", "Inexistente"};
        auto pick = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); };
        Scene scene;
        bool ok = true;
        for (int step = 0; step < 3000 && ok; ++step) {
            const auto& all = scene.Entities();
            auto any = [&]() { return all.empty() ? kNullEntity : all[static_cast<size_t>(pick(int(all.size())))]->id; };
            switch (pick(9)) {
            case 0:
            case 1: scene.CreateEntity(names[static_cast<size_t>(pick(4))], pick(3) == 0 ? kNullEntity : any()); break;
            case 2: if (Entity* e = scene.Find(any())) e->Add<LightComponent>(); break;
            case 3: if (Entity* e = scene.Find(any())) e->Add<MeshRenderer>(); break;
            case 4: if (Entity* e = scene.Find(any())) e->Remove<LightComponent>(); break;
            case 5: scene.SetParent(any(), pick(3) == 0 ? kNullEntity : any(), pick(2) == 0); break;
            case 6: if (Entity* e = scene.Find(any())) e->name = names[static_cast<size_t>(pick(4))]; break;
            case 7: if (all.size() > 40) scene.DestroyEntities({any(), any(), any()}); break;
            case 8: if (pick(20) == 0 && !all.empty()) scene.Duplicate(any()); break;
            }
            // Às vezes consulta no meio (aplica as mudanças guardadas), às vezes deixa acumular.
            if (pick(4) == 0) ok = Consistent(scene, names);
            if (pick(500) == 0) scene = Scene(scene);  // cópia movida para o lugar: índice novo
        }
        CHECK(ok);
        CHECK(Consistent(scene, names));
    }

    return TestResult();
}
