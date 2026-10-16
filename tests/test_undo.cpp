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

    SECTION("Reset limpa tudo");
    h.Reset(scene, selected);
    CHECK(!h.CanUndo() && !h.CanRedo());

    return TestResult();
}
