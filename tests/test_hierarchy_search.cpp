// Busca da Hierarquia (Fase 20) sem janela: palavras no nome e t:componente, sem diferença de maiúsculas e
// acentos, resultados na ordem da Hierarquia e o caminho do pai mostrado ao lado.
#include "TestUtil.h"

#include "panels/HierarchyFilter.h"

#include <motor/Motor.h>

using namespace editor;
using namespace motor;

namespace {
std::vector<EntityId> Search(const Scene& scene, const char* text) {
    return SearchHierarchy(scene, HierarchyFilter::Parse(text));
}
} // namespace

int main() {
    SECTION("Texto dobrado: minúsculas e sem acentos");
    CHECK(FoldForSearch("Ação Pública") == "acao publica");
    CHECK(FoldForSearch("ÉÊÍÓÔÕÚÇÃ") == "eeiooouca");
    CHECK(FoldForSearch("Torre 2") == "torre 2");
    CHECK(FoldForSearch("ñ ü") == "n u");

    SECTION("Palavras e t:");
    {
        const HierarchyFilter f = HierarchyFilter::Parse("  Porta   t:Collider t: ");
        CHECK(f.names.size() == 1 && f.names[0] == "porta");
        CHECK(f.types.size() == 1 && f.types[0] == "collider");  // "t:" sozinho ainda está sendo digitado
        CHECK(HierarchyFilter::Parse("   ").Empty());
        CHECK(HierarchyFilter::Parse("t:").Empty());
    }

    Scene scene;
    const EntityId casa = scene.CreateEntity("Casa").id;
    const EntityId porta = scene.CreateEntity("Porta da Frente", casa).id;
    scene.Find(porta)->Add<ColliderComponent>();
    const EntityId luz = scene.CreateEntity("Luz da Sala", casa).id;
    scene.Find(luz)->Add<LightComponent>();
    const EntityId portao = scene.CreateEntity("Portão").id;
    scene.Find(portao)->Add<ColliderComponent>();
    scene.Find(portao)->Add<MeshRenderer>();
    const EntityId sol = scene.CreateEntity("Sol").id;
    scene.Find(sol)->Add<LightComponent>();

    SECTION("Nome: parte do nome, sem maiúsculas nem acentos, em ordem da Hierarquia");
    {
        const auto found = Search(scene, "PORT");
        CHECK(found.size() == 2 && found[0] == porta && found[1] == portao);
        CHECK(Search(scene, "portao") == std::vector<EntityId>{portao});  // sem o til
        CHECK(Search(scene, "da sala").size() == 1);                         // duas palavras: as duas batem
        CHECK(Search(scene, "porta sala").empty());
    }

    SECTION("Tipo de componente, pelo rótulo ou pelo nome no código");
    {
        const auto lights = Search(scene, "t:luz");
        CHECK(lights.size() == 2 && lights[0] == luz && lights[1] == sol);
        const auto colliders = Search(scene, "t:collider");
        CHECK(colliders.size() == 2 && colliders[0] == porta && colliders[1] == portao);
        CHECK(Search(scene, "t:MeshRenderer") == std::vector<EntityId>{portao});
        CHECK(Search(scene, "t:collider t:mesh") == std::vector<EntityId>{portao});
        CHECK(Search(scene, "frente t:collider") == std::vector<EntityId>{porta});
        CHECK(Search(scene, "t:rigidbody").empty());
    }

    SECTION("Caminho do pai");
    CHECK(ParentPath(scene, porta) == "Casa");
    CHECK(ParentPath(scene, casa).empty());
    const EntityId macaneta = scene.CreateEntity("Maçaneta", porta).id;
    CHECK(ParentPath(scene, macaneta) == "Casa / Porta da Frente");
    CHECK(Search(scene, "macaneta") == std::vector<EntityId>{macaneta});

    return TestResult();
}
