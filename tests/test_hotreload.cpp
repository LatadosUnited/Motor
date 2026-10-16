// Base da recarga de scripts, sem DLL: componente de tipo desconhecido preservado (MissingComponent),
// guardar o estado dos componentes antes de desregistrar o tipo e restaurá-lo com a versão nova do tipo.
#include "TestUtil.h"

#include "UndoHistory.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/scene/Component.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneSerializer.h>
#include <motor/scene/Script.h>

#include <string>

using namespace motor;

namespace {
// "Versão 1" de um script.
struct PuloV1 : Script {
    float altura = 1.0f;
    std::string nome = "padrão";
};

// "Versão 2" do mesmo script (mesmo nome registrado): perdeu "nome", ganhou "velocidade".
struct PuloV2 : Script {
    float altura = 1.0f;
    float velocidade = 3.0f;
};

struct Fantasma {
    int x = 0;
    glm::vec3 v{0.0f};
};

const ComponentType* RegisterV1() {
    ComponentRegistry::Instance().Register<PuloV1>("Pulo", "Pulo")
        .Field("altura", "Altura", &PuloV1::altura)
        .Field("nome", "Nome", &PuloV1::nome);
    return ComponentRegistry::TypeOf<PuloV1>();
}

const ComponentType* RegisterV2() {
    ComponentRegistry::Instance().Register<PuloV2>("Pulo", "Pulo")
        .Field("altura", "Altura", &PuloV2::altura)
        .Field("velocidade", "Velocidade", &PuloV2::velocidade);
    return ComponentRegistry::TypeOf<PuloV2>();
}

bool IsMissing(const ComponentPtr& c) {
    return &c.Type() == &ComponentRegistry::MissingType();
}

const MissingComponent& AsMissing(const ComponentPtr& c) {
    return *static_cast<const MissingComponent*>(c.Data());
}

const char* kSceneWithUnknown = R"({
  "format": "motor-scene",
  "version": 4,
  "entities": [
    {"id": 1, "name": "A", "components": [
      {"type": "Camera", "fov": 50.0},
      {"type": "Fantasma", "x": 7, "v": [1.0, 2.0, 3.0], "extra": {"aninhado": true}},
      {"type": "Outro", "s": "ação"}
    ]}
  ]
})";
} // namespace

int main() {
    AssetLibrary assets;
    ComponentRegistry& registry = ComponentRegistry::Instance();

    SECTION("Componente desconhecido é mantido ao carregar e salvar");
    Scene scene;
    std::string error;
    CHECK(SceneSerializer::FromJson(kSceneWithUnknown, scene, assets, &error));
    CHECK(scene.Entities().size() == 1);
    Entity& a = *scene.Entities()[0];
    CHECK(a.components.size() == 3);
    CHECK(a.Has<CameraComponent>() && a.Get<CameraComponent>()->fov == 50.0f);
    CHECK(IsMissing(a.components[1]) && AsMissing(a.components[1]).type == "Fantasma");
    CHECK(IsMissing(a.components[2]) && AsMissing(a.components[2]).type == "Outro");
    CHECK(registry.Find("MissingComponent") == nullptr);  // o tipo interno não aparece no registro
    {
        const std::string saved = SceneSerializer::ToJson(scene, assets);
        CHECK(saved.find("\"type\": \"Fantasma\"") != std::string::npos);
        CHECK(saved.find("\"aninhado\": true") != std::string::npos);
        CHECK(saved.find("ação") != std::string::npos);
        Scene again;
        CHECK(SceneSerializer::FromJson(saved, again, assets));
        CHECK(again == scene);  // ida e volta sem perder nada
        CHECK(SceneSerializer::ToJson(again, assets) == saved);
        const Scene copy = scene;  // cópia (desfazer, Play) também guarda
        CHECK(copy == scene && IsMissing(copy.Entities()[0]->components[1]));
    }

    SECTION("O desconhecido é religado quando o tipo aparece (na mesma posição)");
    registry.Register<Fantasma>("Fantasma", "Fantasma").Field("x", "X", &Fantasma::x).Field("v", "V", &Fantasma::v);
    CHECK(SceneSerializer::ResolveMissingComponents(scene, assets) == 1);
    {
        Entity& e = *scene.Entities()[0];
        CHECK(e.components.size() == 3);
        CHECK(&e.components[1].Type() == registry.Find("Fantasma"));
        const Fantasma* f = e.Get<Fantasma>();
        CHECK(f && f->x == 7 && f->v == glm::vec3(1.0f, 2.0f, 3.0f));
        CHECK(IsMissing(e.components[2]));  // "Outro" continua guardado
        CHECK(SceneSerializer::ResolveMissingComponents(scene, assets) == 0);
    }

    SECTION("Recarga: guarda o estado, desregistra, registra a versão nova e restaura");
    {
        const ComponentType* v1 = RegisterV1();
        CHECK(v1 && v1 == registry.Find("Pulo") && v1->IsScript());

        Scene s;
        Entity& e = s.CreateEntity("Saltador");
        PuloV1& p = e.Add<PuloV1>();
        p.altura = 2.5f;
        p.nome = "Zé";
        e.Add<CameraComponent>();
        const EntityId id = e.id;

        // Histórico com fotos que também têm o componente (desfazer depois da recarga tem que funcionar).
        editor::UndoHistory history;
        history.Reset(s, id);
        s.Find(id)->Get<PuloV1>()->altura = 4.0f;
        history.Track(s, id, true);
        CHECK(history.UndoCount() == 1);

        auto forEach = [&](const std::function<void(Scene&)>& fn) {
            fn(s);
            history.ForEachScene(fn);
        };
        size_t detached = 0;
        forEach([&](Scene& sc) {
            detached += SceneSerializer::DetachComponents(sc, [&](const ComponentType& t) { return &t == v1; }, assets);
        });
        CHECK(detached == 3);  // cena, foto atual e a foto antiga do desfazer
        CHECK(IsMissing(s.Find(id)->components[0]));
        CHECK(s.Find(id)->Has<CameraComponent>());  // outros tipos não mexem
        const std::string jsonDetached = SceneSerializer::ToJson(s, assets);
        CHECK(jsonDetached.find("\"nome\": \"Zé\"") != std::string::npos);

        registry.Unregister(v1);
        CHECK(registry.Find("Pulo") == nullptr);
        CHECK(ComponentRegistry::TypeOf<PuloV1>() == nullptr);  // cache de TypeOf invalidado

        const ComponentType* v2 = RegisterV2();
        size_t resolved = 0;
        forEach([&](Scene& sc) { resolved += SceneSerializer::ResolveMissingComponents(sc, assets); });
        CHECK(resolved == 3);
        const PuloV2* p2 = s.Find(id)->Get<PuloV2>();
        CHECK(p2 && p2->altura == 4.0f && p2->velocidade == 3.0f);  // campo novo: valor padrão
        CHECK(&s.Find(id)->components[0].Type() == v2);             // mesma posição na lista

        // A cena religada é igual à foto religada: nenhuma alteração "fantasma" no desfazer.
        history.Track(s, id, true);
        CHECK(history.UndoCount() == 1);
        EntityId selected = id;
        CHECK(history.Undo(s, selected));
        const PuloV2* undone = s.Find(id)->Get<PuloV2>();
        CHECK(undone && undone->altura == 2.5f);

        SECTION("Script removido: os dados ficam guardados até o tipo voltar");
        forEach([&](Scene& sc) {
            SceneSerializer::DetachComponents(sc, [&](const ComponentType& t) { return &t == v2; }, assets);
        });
        registry.Unregister(v2);
        size_t none = 0;
        forEach([&](Scene& sc) { none += SceneSerializer::ResolveMissingComponents(sc, assets); });
        CHECK(none == 0);
        CHECK(IsMissing(s.Find(id)->components[0]) && AsMissing(s.Find(id)->components[0]).type == "Pulo");
        const std::string saved = SceneSerializer::ToJson(s, assets);
        CHECK(saved.find("\"altura\": 2.5") != std::string::npos);

        const ComponentType* back = RegisterV1();  // voltou (com outro struct, mesmo nome)
        CHECK(SceneSerializer::ResolveMissingComponents(s, assets) == 1);
        const PuloV1* p1 = s.Find(id)->Get<PuloV1>();
        CHECK(p1 && p1->altura == 2.5f && p1->nome == "padrão");  // "nome" sumiu no meio do caminho
        CHECK(&s.Find(id)->components[0].Type() == back);
        forEach([&](Scene& sc) {
            SceneSerializer::DetachComponents(sc, [&](const ComponentType& t) { return &t == back; }, assets);
        });
        registry.Unregister(back);
    }

    SECTION("Entidade que já tem o tipo não ganha um segundo");
    {
        Scene s;
        CHECK(SceneSerializer::FromJson(R"({"format": "motor-scene", "version": 3, "entities": [
            {"id": 1, "name": "B", "components": [{"type": "Pulo", "altura": 9.0}]}]})",
                                        s, assets));
        const ComponentType* v2 = RegisterV2();
        Entity& b = *s.Entities()[0];
        b.components.insert(b.components.begin(), ComponentPtr(*v2));  // já tem um Pulo
        CHECK(SceneSerializer::ResolveMissingComponents(s, assets) == 0);
        CHECK(b.components.size() == 2 && IsMissing(b.components[1]));
        b.components.clear();
        registry.Unregister(v2);
    }

    return TestResult();
}
