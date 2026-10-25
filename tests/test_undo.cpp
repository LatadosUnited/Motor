// Scene (cópia/comparação) e o UndoHistory do editor. Não usa OpenGL.
#include "TestUtil.h"

#include "UndoHistory.h"

using namespace motor;
using editor::UndoHistory;

int main() {
    SECTION("Cópia de cena");
    Scene a;
    Entity& root = a.CreateEntity("Raiz");
    const EntityId rootId = root.id;
    Entity& child = a.CreateEntity("Filho", rootId);
    const EntityId childId = child.id;
    child.transform.position = {1, 2, 3};
    child.Add<MeshRenderer>();

    Scene b = a;
    CHECK(b == a);
    CHECK(b.Find(childId) && b.Find(childId)->parent == rootId);
    CHECK(b.Find(childId) != a.Find(childId));  // entidades independentes
    b.Find(childId)->transform.position.x = 9;
    CHECK(a.Find(childId)->transform.position.x == 1);
    CHECK(!(b == a));
    b = a;
    CHECK(b == a);
    b.environment.ambient.r = 0.5f;
    CHECK(!(b == a));
    b = a;
    b.Find(childId)->Get<MeshRenderer>()->material.specular = 0.9f;
    CHECK(!(b == a));
    // A cópia continua criando ids novos sem colidir.
    Scene c = a;
    CHECK(c.CreateEntity("Nova").id != childId);
    // Cena montada a partir das entidades (o desfazer guarda só elas): igual, com o próximo id pedido.
    {
        std::vector<const Entity*> entities;
        for (const auto& e : a.Entities()) entities.push_back(e.get());
        Scene d = Scene::FromEntities(entities, 50, a.environment);
        CHECK(d == a);
        CHECK(d.NextEntityId() == 50);
        CHECK(d.Find(childId) != a.Find(childId) && SameEntity(*d.Find(childId), *a.Find(childId)));
        CHECK(d.Children(rootId).size() == 1);
        CHECK(Scene::FromEntities(entities, 1, a.environment).NextEntityId() == childId + 1);  // nunca repete id
    }

    SECTION("Histórico: um arraste vira um passo");
    Scene scene = a;
    EntityId selected = childId;
    UndoHistory h;
    h.Reset(scene, selected);
    CHECK(!h.CanUndo() && !h.CanRedo());
    for (int i = 1; i <= 10; ++i) {
        scene.Find(childId)->transform.position.x = 1.0f + static_cast<float>(i);
        h.Track(scene, selected, false);  // arrastando
    }
    CHECK(!h.CanUndo());
    h.Track(scene, selected, true);  // soltou
    CHECK(h.UndoCount() == 1);
    h.Track(scene, selected, true);  // nada mudou
    CHECK(h.UndoCount() == 1);

    SECTION("Desfazer / refazer");
    CHECK(h.Undo(scene, selected));
    CHECK(scene.Find(childId)->transform.position.x == 1.0f);
    CHECK(h.CanRedo() && !h.CanUndo());
    CHECK(h.Redo(scene, selected));
    CHECK(scene.Find(childId)->transform.position.x == 11.0f);
    CHECK(!h.Redo(scene, selected));

    SECTION("Alteração nova descarta o refazer");
    h.Undo(scene, selected);
    scene.Find(rootId)->name = "Outro nome";
    h.Track(scene, selected, true);
    CHECK(!h.CanRedo());
    CHECK(h.UndoCount() == 1);

    SECTION("Excluir e desfazer devolve a entidade (e a seleção)");
    selected = rootId;
    h.Track(scene, selected, true);   // só seleção: não é passo
    CHECK(h.UndoCount() == 1);
    scene.DestroyEntity(rootId);      // leva o filho junto
    selected = kNullEntity;
    h.Track(scene, selected, true);
    CHECK(!scene.Find(childId));
    CHECK(h.Undo(scene, selected));
    CHECK(scene.Find(rootId) && scene.Find(childId));
    CHECK(scene.Find(childId)->parent == rootId);
    CHECK(selected == rootId);

    SECTION("Desfazer com mudança ainda não registrada");
    scene.Find(childId)->name = "Renomeado";   // sem Track (ex.: atalho no mesmo frame)
    CHECK(h.Undo(scene, selected));
    CHECK(scene.Find(childId)->name == "Filho");
    CHECK(h.Redo(scene, selected));
    CHECK(scene.Find(childId)->name == "Renomeado");

    SECTION("Limite de passos");
    for (int i = 0; i < 150; ++i) {
        scene.Find(childId)->transform.position.y = static_cast<float>(i);
        h.Track(scene, selected, true);
    }
    CHECK(h.UndoCount() == UndoHistory::kMaxSteps);

    SECTION("Versão: muda a cada passo e volta ao desfazer (o \"não salvo\" do editor)");
    {
        const uint64_t saved = h.Version();
        CHECK(!h.HasPendingChange(scene));
        scene.Find(childId)->name = "Versão nova";
        CHECK(h.HasPendingChange(scene));
        CHECK(h.Version() == saved);  // ainda não virou passo
        h.Track(scene, selected, true);
        CHECK(!h.HasPendingChange(scene));
        const uint64_t changed = h.Version();
        CHECK(changed != saved);
        CHECK(h.Undo(scene, selected));
        CHECK(h.Version() == saved);
        CHECK(h.Redo(scene, selected));
        CHECK(h.Version() == changed);
        h.Track(scene, selected, true);  // nada mudou: a versão fica
        CHECK(h.Version() == changed);
    }

    SECTION("Desfazer a criação devolve o próximo id (FromEntities)");
    {
        const EntityId created = scene.CreateEntity("Nova").id;
        h.Track(scene, selected, true);
        CHECK(h.Undo(scene, selected));
        CHECK(!scene.Find(created));
        CHECK(scene.NextEntityId() == created);
        CHECK(h.Redo(scene, selected));
        CHECK(scene.Find(created) && scene.Find(created)->name == "Nova");
        CHECK(scene.CreateEntity("Outra").id == created + 1);
        CHECK(scene.Children(kNullEntity).size() >= 2);  // os índices da cena montada funcionam
        h.Track(scene, selected, true);
    }

    SECTION("ForEachScene troca as fotos e o desfazer continua");
    {
        const size_t steps = h.UndoCount();
        int scenes = 0;
        h.ForEachScene([&](Scene& s) {
            ++scenes;
            if (Entity* e = s.Find(childId)) e->transform.scale = {2, 2, 2};
        });
        CHECK(scenes == static_cast<int>(steps) + 1);
        scene.Find(childId)->transform.scale = {2, 2, 2};  // a cena aberta recebe a mesma troca (como os scripts)
        CHECK(!h.HasPendingChange(scene));
        CHECK(h.Undo(scene, selected));
        CHECK(scene.Find(childId)->transform.scale == glm::vec3(2, 2, 2));
        CHECK(h.Redo(scene, selected));
    }

    SECTION("Comparação preguiçosa: só depois de atividade (editor)");
    {
        UndoHistory lazy;
        Scene s = a;
        EntityId sel = kNullEntity;
        lazy.SetLazyCompare(true);
        lazy.Reset(s, sel);
        s.Find(childId)->name = "Mudou";
        lazy.Track(s, sel, true);  // sem atividade e logo depois do Reset: não compara
        CHECK(lazy.UndoCount() == 0);
        CHECK(lazy.HasPendingChange(s));
        lazy.NoteActivity();
        lazy.Track(s, sel, true);
        CHECK(lazy.UndoCount() == 1);
        s.Find(childId)->name = "Mudou de novo";
        lazy.Track(s, sel, true);
        CHECK(lazy.UndoCount() == 1);
        CHECK(lazy.Undo(s, sel));  // desfazer compara antes: a mudança pendente vira passo e é desfeita
        CHECK(s.Find(childId)->name == "Mudou");
    }

    SECTION("Reset limpa tudo");
    h.Reset(scene, selected);
    CHECK(!h.CanUndo() && !h.CanRedo());

    return TestResult();
}
