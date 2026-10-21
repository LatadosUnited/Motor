// Listas por tipo de componente da cena (Fase 25): Scene::EntitiesWith devolve as entidades que têm o componente sem
// percorrer a cena a cada chamada. Confere que o resultado é sempre o mesmo de percorrer tudo: ordem da cena, criar,
// destruir, adicionar e remover componentes, cópia, cena movida e mudanças feitas direto em Entity::components.
#include "TestUtil.h"

#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>

#include <vector>

using namespace motor;

namespace {

// O que EntitiesWith deve devolver: a varredura da cena inteira.
template <class T> std::vector<EntityId> Scan(const Scene& scene) {
    std::vector<EntityId> ids;
    for (const auto& e : scene.Entities()) {
        if (e->Has<T>()) ids.push_back(e->id);
    }
    return ids;
}

template <class T> bool Same(const Scene& scene) {
    return scene.EntitiesWith<T>() == Scan<T>(scene);
}

} // namespace

int main() {
    SECTION("ordem da cena e entidades sem o componente");
    {
        Scene scene;
        const EntityId a = scene.CreateEntity("A").id;
        scene.CreateEntity("Sem nada");
        Entity& b = scene.CreateEntity("B");
        b.Add<LightComponent>();
        scene.Find(a)->Add<LightComponent>();
        CHECK((scene.EntitiesWith<LightComponent>() == std::vector<EntityId>{a, b.id}));
        CHECK(scene.EntitiesWith<AudioSource>().empty());
        CHECK(Same<LightComponent>(scene));
    }

    SECTION("criar, destruir, adicionar e remover");
    {
        Scene scene;
        Entity& pai = scene.CreateEntity("Pai");
        pai.Add<AudioSource>();
        const EntityId paiId = pai.id;
        const EntityId filho = scene.CreateEntity("Filho", paiId).id;
        scene.Find(filho)->Add<AudioSource>();
        CHECK(scene.EntitiesWith<AudioSource>().size() == 2);

        const EntityId outra = scene.CreateEntity("Outra").id;
        scene.Find(outra)->Add<AudioSource>();
        CHECK(Same<AudioSource>(scene));

        scene.Find(filho)->Remove<AudioSource>();
        CHECK(Same<AudioSource>(scene));
        CHECK(scene.EntitiesWith<AudioSource>().size() == 2);

        scene.DestroyEntity(paiId);  // leva o filho junto
        CHECK((scene.EntitiesWith<AudioSource>() == std::vector<EntityId>{outra}));

        scene.Find(outra)->Add<AudioSource>();  // já tem: nada muda
        CHECK(Same<AudioSource>(scene));
    }

    SECTION("vários tipos na mesma entidade e componentes desativados");
    {
        Scene scene;
        Entity& e = scene.CreateEntity("Caixa");
        e.Add<ColliderComponent>();
        e.Add<RigidbodyComponent>();
        e.active = false;  // inativa continua na lista: quem usa confere IsActiveInHierarchy
        CHECK(Same<ColliderComponent>(scene));
        CHECK(Same<RigidbodyComponent>(scene));
        CHECK(scene.EntitiesWith<ColliderComponent>().size() == 1);
    }

    SECTION("cópia, duplicar e cena movida");
    {
        Scene scene;
        scene.CreateEntity("Luz").Add<LightComponent>();
        CHECK(scene.EntitiesWith<LightComponent>().size() == 1);

        Scene copia = scene;
        CHECK(Same<LightComponent>(copia));
        copia.Duplicate(copia.EntitiesWith<LightComponent>().front());
        CHECK(copia.EntitiesWith<LightComponent>().size() == 2);
        CHECK(scene.EntitiesWith<LightComponent>().size() == 1);  // a outra cena refaz a sua lista

        Scene movida = std::move(copia);
        CHECK(Same<LightComponent>(movida));
        scene = movida;
        CHECK(scene.EntitiesWith<LightComponent>().size() == 2);
    }

    SECTION("mudança direta em Entity::components com MarkSceneStructureChanged");
    {
        Scene scene;
        Entity& e = scene.CreateEntity("E");
        CHECK(scene.EntitiesWith<AudioListener>().empty());
        e.components.emplace_back(*ComponentRegistry::TypeOf<AudioListener>());
        MarkSceneStructureChanged();
        CHECK(Same<AudioListener>(scene));
        e.components.clear();
        MarkSceneStructureChanged();
        CHECK(scene.EntitiesWith<AudioListener>().empty());
    }

    SECTION("câmera principal e luzes pela lista");
    {
        Scene scene;
        Entity& c1 = scene.CreateEntity("Câmera 1");
        c1.Add<CameraComponent>().main = false;
        const EntityId id1 = c1.id;
        CHECK(scene.FindMainCamera() == id1);  // sem principal: a primeira ativa
        Entity& c2 = scene.CreateEntity("Câmera 2");
        c2.Add<CameraComponent>().main = true;
        CHECK(scene.FindMainCamera() == c2.id);
        scene.Find(c2.id)->active = false;  // não sobe a versão: a lista confere na hora
        CHECK(scene.FindMainCamera() == id1);

        scene.CreateEntity("Sol").Add<LightComponent>();
        Entity& apagada = scene.CreateEntity("Apagada");
        apagada.Add<LightComponent>();
        apagada.active = false;
        CHECK(scene.CollectLights().size() == 1);
    }

    return TestResult();
}
