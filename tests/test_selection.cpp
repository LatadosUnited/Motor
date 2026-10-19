// Seleção múltipla do editor (Fase 20) sem janela: o conjunto no EditorContext (alternar, raízes, sincronizar
// com quem só troca a principal), duplicar e excluir vários, o desfazer guardando a seleção e o Inspector
// repetindo nos outros selecionados o que mudou na principal (MultiEdit).
#include "TestUtil.h"

#include "EditorContext.h"
#include "UndoHistory.h"
#include "panels/MultiEdit.h"

#include <motor/Motor.h>

#include <glm/trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

using namespace editor;
using namespace motor;

namespace {
bool Near(float a, float b) { return std::abs(a - b) < 1e-3f; }

// Componente com referência a outra entidade (as cópias precisam ajustá-la).
struct Mirar {
    EntityRef alvo;
};
} // namespace

int main() {
    SECTION("Alternar, somar e a principal");
    {
        EditorContext ctx;
        const EntityId a = ctx.scene.CreateEntity("A").id;
        const EntityId b = ctx.scene.CreateEntity("B").id;
        const EntityId c = ctx.scene.CreateEntity("C").id;

        ctx.SelectOnly(a);
        CHECK(ctx.selected == a && ctx.SelectionCount() == 1);
        ctx.ToggleSelected(b);  // Ctrl+clique: entra como principal
        CHECK(ctx.selected == b && ctx.IsSelected(a) && ctx.IsSelected(b));
        ctx.AddToSelection(c);
        CHECK(ctx.selected == c && ctx.SelectionCount() == 3);
        ctx.ToggleSelected(c);  // sai a principal: a anterior assume
        CHECK(ctx.selected == b && !ctx.IsSelected(c) && ctx.SelectionCount() == 2);
        ctx.ToggleSelected(a);
        ctx.ToggleSelected(b);
        CHECK(ctx.selected == kNullEntity && ctx.SelectionCount() == 0);
        ctx.SetSelection({a, c});
        CHECK(ctx.selected == c && ctx.SelectionCount() == 2);
        ctx.SelectOnly(kNullEntity);
        CHECK(ctx.selected == kNullEntity && ctx.selection.empty());
        ctx.AddToSelection(999);  // inexistente: ignorada
        CHECK(ctx.selection.empty());
    }

    SECTION("Quem só troca a principal (criar, MCP, desfazer) reduz a seleção a ela");
    {
        EditorContext ctx;
        const EntityId a = ctx.scene.CreateEntity("A").id;
        const EntityId b = ctx.scene.CreateEntity("B").id;
        ctx.SetSelection({a, b});
        Entity& novo = ctx.CreatePrimitive(Primitive::Cube);  // ctx.selected = novo
        ctx.SyncSelection();
        CHECK(ctx.selected == novo.id && ctx.SelectionCount() == 1);
        // Principal trocada para outra que já estava selecionada: o grupo fica, ela vai para o fim.
        ctx.SetSelection({a, b});
        ctx.selected = a;
        ctx.SyncSelection();
        CHECK(ctx.SelectionCount() == 2 && ctx.selection.back() == a);
        // Entidade que saiu da cena sai da seleção.
        ctx.scene.DestroyEntity(b);
        ctx.SyncSelection();
        CHECK(ctx.SelectionCount() == 1 && ctx.selected == a);
        ctx.selected = kNullEntity;  // Esc antigo
        ctx.SyncSelection();
        CHECK(ctx.selection.empty());
    }

    SECTION("Raízes: filho de outro selecionado não conta duas vezes");
    {
        EditorContext ctx;
        const EntityId pai = ctx.scene.CreateEntity("Pai").id;
        const EntityId filho = ctx.scene.CreateEntity("Filho", pai).id;
        const EntityId neto = ctx.scene.CreateEntity("Neto", filho).id;
        const EntityId solto = ctx.scene.CreateEntity("Solto").id;
        ctx.SetSelection({neto, pai, solto});
        const auto roots = ctx.SelectionRoots();
        CHECK(roots.size() == 2);
        CHECK(std::find(roots.begin(), roots.end(), pai) != roots.end());
        CHECK(std::find(roots.begin(), roots.end(), solto) != roots.end());
    }

    SECTION("Duplicar e excluir vários");
    {
        EditorContext ctx;
        const EntityId a = ctx.scene.CreateEntity("A").id;
        const EntityId filho = ctx.scene.CreateEntity("Filho", a).id;
        const EntityId b = ctx.scene.CreateEntity("B").id;
        const EntityId fica = ctx.scene.CreateEntity("Fica").id;
        ctx.SetSelection({a, filho, b});
        const size_t before = ctx.scene.Entities().size();
        ctx.DuplicateSelected();
        // A com o filho (2) e B (1): o filho não é copiado duas vezes.
        CHECK(ctx.scene.Entities().size() == before + 3);
        CHECK(ctx.SelectionCount() == 2 && !ctx.IsSelected(a) && !ctx.IsSelected(b));

        ctx.SetSelection({a, b});
        ctx.pendingDelete = ctx.selected;  // Del
        ctx.ApplyPendingDelete();
        CHECK(!ctx.scene.Find(a) && !ctx.scene.Find(filho) && !ctx.scene.Find(b));
        CHECK(ctx.scene.Find(fica) && ctx.selection.empty() && ctx.selected == kNullEntity);

        // Excluir uma que não está selecionada não mexe na seleção.
        const EntityId x = ctx.scene.CreateEntity("X").id;
        const EntityId y = ctx.scene.CreateEntity("Y").id;
        ctx.SetSelection({x, y});
        ctx.pendingDelete = fica;
        ctx.ApplyPendingDelete();
        CHECK(!ctx.scene.Find(fica) && ctx.SelectionCount() == 2);
    }

    SECTION("Desfazer devolve a seleção inteira");
    {
        EditorContext ctx;
        const EntityId a = ctx.scene.CreateEntity("A").id;
        const EntityId b = ctx.scene.CreateEntity("B").id;
        UndoHistory h;
        h.Reset(ctx.scene, ctx.selected);
        ctx.SetSelection({a, b});
        h.Track(ctx.scene, ctx.selected, true, &ctx.selection);  // só seleção: não é passo
        CHECK(!h.CanUndo());
        ctx.pendingDelete = ctx.selected;
        ctx.ApplyPendingDelete();
        h.Track(ctx.scene, ctx.selected, true, &ctx.selection);
        CHECK(h.CanUndo() && ctx.selection.empty());
        CHECK(h.Undo(ctx.scene, ctx.selected, &ctx.selection));
        CHECK(ctx.scene.Find(a) && ctx.scene.Find(b));
        CHECK(ctx.selected == b && ctx.SelectionCount() == 2 && ctx.IsSelected(a));
        CHECK(h.Redo(ctx.scene, ctx.selected, &ctx.selection));
        CHECK(!ctx.scene.Find(a) && ctx.selection.empty());
    }

    SECTION("Inspector com vários: só o que mudou vai para os outros");
    {
        AssetLibrary assets;
        Scene scene;
        Entity& principal = scene.CreateEntity("Principal");
        principal.transform.position = {1, 2, 3};
        principal.Add<LightComponent>().intensity = 1.0f;
        Entity& outro = scene.CreateEntity("Outro");
        outro.transform.position = {7, 8, 9};
        outro.transform.SetEulerDegrees({10.0f, 20.0f, 0.0f});
        outro.Add<LightComponent>().intensity = 5.0f;
        outro.Get<LightComponent>()->range = 42.0f;
        Entity& semLuz = scene.CreateEntity("SemLuz");

        const Entity before = principal;
        principal.transform.position.y = 0.5f;  // arrastou só o Y
        principal.transform.SetEulerDegrees({0.0f, 0.0f, 30.0f});  // mudou só o Z
        principal.Get<LightComponent>()->intensity = 3.0f;
        principal.name = "Renomeada";
        PropagateEdits(before, principal, outro, assets);
        PropagateEdits(before, principal, semLuz, assets);

        CHECK(outro.transform.position == glm::vec3(7, 0.5f, 9));
        const glm::vec3 euler = glm::degrees(glm::eulerAngles(outro.transform.rotation));
        CHECK(Near(euler.x, 10.0f) && Near(euler.y, 20.0f) && Near(euler.z, 30.0f));
        CHECK(outro.Get<LightComponent>()->intensity == 3.0f);
        CHECK(outro.Get<LightComponent>()->range == 42.0f);  // campo não mexido fica
        CHECK(outro.name == "Outro");
        CHECK(!semLuz.Has<LightComponent>());  // quem não tem o componente não ganha por mudança de campo

        // Componente adicionado e removido na principal.
        const Entity before2 = principal;
        principal.Add<MeshRenderer>();
        principal.Remove<LightComponent>();
        principal.active = false;
        PropagateEdits(before2, principal, outro, assets);
        PropagateEdits(before2, principal, semLuz, assets);
        CHECK(outro.Has<MeshRenderer>() && !outro.Has<LightComponent>() && !outro.active);
        CHECK(semLuz.Has<MeshRenderer>() && !semLuz.active);

        // Nada mudou: nada muda.
        const Transform quieto = outro.transform;
        const size_t componentes = outro.components.size();
        PropagateEdits(principal, principal, outro, assets);
        CHECK(outro.transform.position == quieto.position && outro.transform.rotation == quieto.rotation);
        CHECK(outro.components.size() == componentes);
    }

    SECTION("Copiar e colar: mesma cena, sob outro pai, no mesmo lugar do mundo");
    {
        ComponentRegistry::Instance().Register<Mirar>("TesteMirar", "Mirar").Field("alvo", &Mirar::alvo);
        EditorContext ctx;
        Entity& pai = ctx.scene.CreateEntity("Pai");
        pai.transform.position = {10, 0, 0};
        const EntityId paiId = pai.id;
        Entity& torre = ctx.scene.CreateEntity("Torre", paiId);
        torre.transform.position = {1, 2, 3};  // no mundo: (11, 2, 3)
        const EntityId torreId = torre.id;
        const EntityId arma = ctx.scene.CreateEntity("Arma", torreId).id;
        const EntityId fora = ctx.scene.CreateEntity("Fora").id;
        ctx.scene.Find(torreId)->Add<Mirar>().alvo = arma;  // aponta para dentro da cópia
        ctx.scene.Find(arma)->Add<Mirar>().alvo = fora;     // aponta para fora

        ctx.SelectOnly(torreId);
        ctx.CopySelected();
        CHECK(ctx.clipboard.Count() == 1);
        ctx.Paste();
        CHECK(ctx.SelectionCount() == 1 && ctx.selected != torreId);
        const Entity* copia = ctx.scene.Find(ctx.selected);
        CHECK(copia && copia->name == "Torre" && copia->parent == kNullEntity);
        CHECK(glm::vec3(ctx.scene.WorldMatrix(copia->id)[3]) == glm::vec3(11, 2, 3));
        const auto filhos = ctx.scene.Children(copia->id);
        CHECK(filhos.size() == 1);
        CHECK(copia->Get<Mirar>()->alvo.id == filhos[0]);              // segue a cópia da arma
        CHECK(ctx.scene.Find(filhos[0])->Get<Mirar>()->alvo.id == fora);  // mesma cena: a mesma entidade

        // Colar de novo, como filho de "Fora": continua no mesmo lugar do mundo.
        ctx.Paste(fora);
        CHECK(ctx.scene.Find(ctx.selected)->parent == fora);
        CHECK(glm::length(glm::vec3(ctx.scene.WorldMatrix(ctx.selected)[3]) - glm::vec3(11, 2, 3)) < 1e-4f);

        SECTION("Recortar e colar em outra cena");
        ctx.SetSelection({torreId, fora});
        ctx.CutSelected();
        CHECK(!ctx.scene.Find(torreId) && !ctx.scene.Find(arma) && !ctx.scene.Find(fora));
        CHECK(ctx.selection.empty() && ctx.clipboard.Count() == 2);
        CHECK(ctx.scene.Find(paiId));

        ctx.scene = Scene();  // abriu outra cena
        ctx.ResetSceneState();
        ctx.scene.CreateEntity("Outra");
        ctx.Paste();
        CHECK(ctx.SelectionCount() == 2);
        const Entity* t = ctx.scene.FindByName("Torre");
        const Entity* a = ctx.scene.FindByName("Arma");
        const Entity* f = ctx.scene.FindByName("Fora");
        CHECK(t && a && f && a->parent == t->id);
        CHECK(t->Get<Mirar>()->alvo.id == a->id);
        // "Fora" foi junto na cópia: a referência segue a colada, mesmo em outra cena.
        CHECK(a->Get<Mirar>()->alvo.id == f->id);
        CHECK(glm::vec3(ctx.scene.WorldMatrix(t->id)[3]) == glm::vec3(11, 2, 3));

        SECTION("Referência para fora da cópia, colada em outra cena, fica vazia");
        ctx.SelectOnly(a->id);
        ctx.CopySelected();  // Arma sem o Fora
        ctx.scene = Scene();
        ctx.ResetSceneState();
        for (int i = 0; i < 5; ++i) ctx.scene.CreateEntity("Enche");  // ids que coincidiriam
        ctx.Paste();
        CHECK(ctx.scene.Find(ctx.selected)->Get<Mirar>()->alvo.IsEmpty());
    }

    return TestResult();
}
