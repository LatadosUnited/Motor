// Cache da matriz de mundo (Fase 25): Scene::WorldMatrix guarda a matriz por entidade e confere a cada consulta se
// o transform, o pai ou o mundo do pai mudaram. Os scripts e o editor mexem direto em Entity::transform e
// Entity::parent, sem avisar: o resultado tem de ser sempre o da conta direta, subindo a hierarquia.
#include "TestUtil.h"

#include <motor/scene/Scene.h>

#include <glm/gtc/epsilon.hpp>

using namespace motor;

namespace {

// A conta sem cache: produto dos transforms locais da raiz até a entidade.
glm::mat4 Direct(const Scene& scene, EntityId id) {
    glm::mat4 m(1.0f);
    for (const Entity* e = scene.Find(id); e; e = scene.Find(e->parent)) m = e->transform.Matrix() * m;
    return m;
}

bool Near(const glm::mat4& a, const glm::mat4& b) {
    for (int c = 0; c < 4; ++c) {
        if (!glm::all(glm::epsilonEqual(a[c], b[c], 1e-4f))) return false;
    }
    return true;
}

bool Matches(const Scene& scene, EntityId id) {
    return Near(scene.WorldMatrix(id), Direct(scene, id));
}

bool AllMatch(const Scene& scene) {
    for (const auto& e : scene.Entities()) {
        if (!Matches(scene, e->id)) return false;
    }
    return true;
}

} // namespace

int main() {
    Scene scene;
    const EntityId avo = scene.CreateEntity("Avô").id;
    const EntityId pai = scene.CreateEntity("Pai", avo).id;
    const EntityId filho = scene.CreateEntity("Filho", pai).id;
    const EntityId outro = scene.CreateEntity("Outro").id;
    scene.Find(avo)->transform.position = {1.0f, 0.0f, 0.0f};
    scene.Find(pai)->transform.position = {0.0f, 2.0f, 0.0f};
    scene.Find(pai)->transform.Rotate({0.0f, 1.0f, 0.0f}, 0.5f);
    scene.Find(filho)->transform.position = {0.0f, 0.0f, 3.0f};
    scene.Find(outro)->transform.scale = {2.0f, 2.0f, 2.0f};

    SECTION("primeira consulta e consulta repetida");
    {
        CHECK(AllMatch(scene));
        CHECK(AllMatch(scene));
    }

    SECTION("mexer direto no transform da entidade, do pai e do avô");
    {
        scene.Find(filho)->transform.position.x = 5.0f;
        CHECK(Matches(scene, filho));
        scene.Find(pai)->transform.scale = {1.0f, 3.0f, 1.0f};
        CHECK(Matches(scene, filho));
        scene.Find(avo)->transform.Rotate({1.0f, 0.0f, 0.0f}, 0.3f);
        CHECK(Matches(scene, filho));  // o filho não mudou, mas o mundo do avô sim
        CHECK(AllMatch(scene));
    }

    SECTION("consultar o filho antes e depois de o avô mudar, sem consultar o pai no meio");
    {
        CHECK(Matches(scene, filho));
        scene.Find(avo)->transform.position.z = -4.0f;
        CHECK(Matches(scene, filho));
    }

    SECTION("trocar de pai: SetParent e mexendo direto em Entity::parent");
    {
        CHECK(scene.SetParent(filho, outro, true));
        CHECK(Matches(scene, filho));
        scene.Find(filho)->parent = pai;  // sem SetParent: a cena não fica sabendo
        CHECK(Matches(scene, filho));
        scene.Find(filho)->parent = kNullEntity;
        CHECK(Matches(scene, filho));
        scene.Find(filho)->parent = outro;
        CHECK(Matches(scene, filho));
        scene.Find(filho)->parent = pai;
        CHECK(AllMatch(scene));
    }

    SECTION("cópia da cena e Duplicate não dividem o cache");
    {
        CHECK(AllMatch(scene));
        Scene copia = scene;
        copia.Find(avo)->transform.position.y = 10.0f;
        CHECK(AllMatch(copia));
        CHECK(AllMatch(scene));  // a original não viu a mudança da cópia
        const EntityId dup = scene.Duplicate(pai);
        scene.Find(dup)->transform.position.x = -7.0f;
        CHECK(AllMatch(scene));
    }

    SECTION("destruir e criar entidades com o cache cheio");
    {
        CHECK(AllMatch(scene));
        const EntityId novoPai = scene.CreateEntity("Novo pai").id;
        scene.Find(novoPai)->transform.position = {0.0f, -1.0f, 0.0f};
        scene.Find(outro)->parent = novoPai;
        CHECK(AllMatch(scene));
        scene.DestroyEntity(novoPai);  // leva o Outro junto
        CHECK(scene.Find(outro) == nullptr);
        CHECK(AllMatch(scene));
    }

    SECTION("ativa na hierarquia pelo ponteiro do pai guardado");
    {
        CHECK(scene.IsActiveInHierarchy(filho));
        scene.Find(avo)->active = false;
        CHECK(!scene.IsActiveInHierarchy(filho));
        scene.Find(avo)->active = true;
        scene.Find(filho)->parent = kNullEntity;
        scene.Find(pai)->active = false;
        CHECK(scene.IsActiveInHierarchy(filho));  // não está mais embaixo do pai desativado
    }

    return TestResult();
}
