// Sistema de componentes: registro, Entity::Add/Get/Remove, cópia/comparação e salvar/carregar na cena.
// Não usa OpenGL (nenhuma malha/textura real é criada).
#include "TestUtil.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/scene/Component.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneSerializer.h>

#include <string>

using namespace motor;

namespace {
// Um componente "de usuário" com todos os tipos de campo suportados.
struct Tudo {
    bool ligado = true;
    int vidas = 3;
    float velocidade = 1.5f;
    glm::vec2 tamanho{1.0f, 2.0f};
    glm::vec3 direcao{0.0f, 1.0f, 0.0f};
    glm::vec4 cor{1.0f, 0.5f, 0.25f, 1.0f};
    std::string apelido = "herói";
    std::shared_ptr<Mesh> malha;
    std::shared_ptr<Texture2D> textura;
    int estadoInterno = 0;  // não registrado: não é salvo nem comparado
};

struct Vazio {};

void RegisterTestComponents() {
    auto& registry = ComponentRegistry::Instance();
    registry.Register<Tudo>("Tudo", "Tudo")
        .Field("ligado", "Ligado", &Tudo::ligado)
        .Field("vidas", "Vidas", &Tudo::vidas)
        .Field("velocidade", "Velocidade", &Tudo::velocidade, {.slider = true, .min = 0.0f, .max = 10.0f})
        .Field("tamanho", "Tamanho", &Tudo::tamanho)
        .Field("direcao", "Direção", &Tudo::direcao)
        .Field("cor", "Cor", &Tudo::cor, {.color = true})
        .Field("apelido", "Apelido", &Tudo::apelido)
        .Field("malha", "Malha", &Tudo::malha)
        .Field("textura", "Textura", &Tudo::textura);
    registry.Register<Vazio>("Vazio", "Vazio");
}
} // namespace

int main() {
    SECTION("Registro");
    const auto& registry = ComponentRegistry::Instance();
    CHECK(registry.Find("MeshRenderer") != nullptr);
    CHECK(ComponentRegistry::TypeOf<MeshRenderer>() == registry.Find("MeshRenderer"));
    CHECK(registry.Find("MeshRenderer")->fields.size() == 18);  // malha, material (asset) e os 16 do material próprio (PBR; alfa, Fase 18)
    RegisterTestComponents();
    const ComponentType* tudo = registry.Find("Tudo");
    CHECK(tudo && tudo->fields.size() == 9);
    CHECK(tudo && tudo->fields[1].type == FieldType::Int && tudo->fields[7].type == FieldType::Mesh);
    CHECK(ComponentRegistry::TypeOf<Tudo>() == tudo);

    SECTION("Entity: Add / Get / Has / Remove");
    Scene scene;
    Entity& e = scene.CreateEntity("Jogador");
    CHECK(!e.Has<Tudo>());
    Tudo& t = e.Add<Tudo>();
    CHECK(e.Has<Tudo>() && e.Get<Tudo>() == &t);
    CHECK(&e.Add<Tudo>() == &t);  // já existe: devolve o mesmo
    CHECK(e.components.size() == 1);
    e.Add<MeshRenderer>();
    e.Add<Vazio>();
    CHECK(e.components.size() == 3);
    e.Remove<Vazio>();
    CHECK(!e.Has<Vazio>() && e.components.size() == 2);
    CHECK(&e.components[0].Type() == tudo);  // ordem de inserção

    SECTION("Cópia e comparação genéricas");
    t.vidas = 7;
    t.estadoInterno = 42;
    Scene copy = scene;
    CHECK(copy == scene);
    const EntityId id = e.id;
    CHECK(copy.Find(id)->Get<Tudo>() != scene.Find(id)->Get<Tudo>());  // cópia profunda
    CHECK(copy.Find(id)->Get<Tudo>()->estadoInterno == 42);
    copy.Find(id)->Get<Tudo>()->estadoInterno = 1;
    CHECK(copy == scene);  // campo não registrado não conta
    copy.Find(id)->Get<Tudo>()->apelido = "vilão";
    CHECK(!(copy == scene));
    copy = scene;
    copy.Find(id)->Get<MeshRenderer>()->material.roughness = 0.9f;
    CHECK(!(copy == scene));
    copy = scene;
    copy.Find(id)->Remove<MeshRenderer>();
    CHECK(!(copy == scene));
    copy = scene;
    copy.Find(id)->Add<Vazio>();
    CHECK(!(copy == scene));

    SECTION("Salvar e carregar (todos os tipos de campo)");
    t.ligado = false;
    t.velocidade = 0.6f;
    t.tamanho = {3.0f, 4.0f};
    t.direcao = {1.0f, 0.0f, -1.0f};
    t.cor = {0.1f, 0.2f, 0.3f, 0.4f};
    t.apelido = "Ação \"entre aspas\"";
    e.Get<MeshRenderer>()->material.color = {0.5f, 0.6f, 0.7f, 1.0f};
    AssetLibrary assets;
    const std::string text = SceneSerializer::ToJson(scene, assets);
    CHECK(text.find("\"version\": 6") != std::string::npos);
    CHECK(text.find("\"type\": \"Tudo\"") != std::string::npos);
    CHECK(text.find("estadoInterno") == std::string::npos);

    Scene loaded;
    std::string error;
    CHECK(SceneSerializer::FromJson(text, loaded, assets, &error));
    const Entity* le = loaded.Entities().empty() ? nullptr : loaded.Entities().front().get();
    CHECK(le && le->components.size() == 2);
    const Tudo* lt = le ? le->Get<Tudo>() : nullptr;
    CHECK(lt && !lt->ligado && lt->vidas == 7 && lt->velocidade == 0.6f);
    CHECK(lt && lt->tamanho == glm::vec2(3.0f, 4.0f) && lt->direcao == glm::vec3(1.0f, 0.0f, -1.0f));
    CHECK(lt && lt->cor == glm::vec4(0.1f, 0.2f, 0.3f, 0.4f));
    CHECK(lt && lt->apelido == "Ação \"entre aspas\"");
    CHECK(lt && !lt->malha && !lt->textura);
    CHECK(le && le->Get<MeshRenderer>() && le->Get<MeshRenderer>()->material.color == glm::vec4(0.5f, 0.6f, 0.7f, 1.0f));
    CHECK(SceneSerializer::ToJson(loaded, assets) == text);  // ida e volta estável

    SECTION("Campos ausentes ficam com o padrão; repetidos são ignorados; desconhecidos ficam guardados");
    const char* partial = R"({"format": "motor-scene", "version": 3, "entities": [
        {"id": 1, "name": "A", "components": [
            {"type": "Tudo", "vidas": 9},
            {"type": "NaoExiste", "x": 1},
            {"type": "Tudo", "vidas": 1},
            {"type": "MeshRenderer", "specular": "texto no lugar de número"}
        ]}]})";
    Scene p;
    CHECK(SceneSerializer::FromJson(partial, p, assets, &error));
    const Entity* pe = p.Entities().empty() ? nullptr : p.Entities().front().get();
    CHECK(pe && pe->components.size() == 3);
    CHECK(pe && pe->Get<Tudo>() && pe->Get<Tudo>()->vidas == 9 && pe->Get<Tudo>()->velocidade == 1.5f);
    CHECK(pe && pe->Get<MeshRenderer>() && pe->Get<MeshRenderer>()->material.specular == 0.5f);
    const auto* faltando = pe ? static_cast<const MissingComponent*>(pe->Get(ComponentRegistry::MissingType())) : nullptr;
    CHECK(faltando && faltando->type == "NaoExiste");
    CHECK(faltando && faltando->json == R"({"type":"NaoExiste","x":1})");

    SECTION("Cena da versão 2 (\"meshRenderer\" fixo) continua abrindo");
    const char* v2 = R"({"format": "motor-scene", "version": 2, "entities": [
        {"id": 1, "name": "Velho", "meshRenderer": {"mesh": null, "texture": null,
         "color": [1, 0, 0, 1], "specular": 0.1, "shininess": 64}}]})";
    Scene old;
    CHECK(SceneSerializer::FromJson(v2, old, assets, &error));
    const Entity* oe = old.Entities().empty() ? nullptr : old.Entities().front().get();
    const MeshRenderer* omr = oe ? oe->Get<MeshRenderer>() : nullptr;
    CHECK(omr && omr->material.color == glm::vec4(1, 0, 0, 1));
    // Brilho (Blinn-Phong) das cenas antigas vira rugosidade (Fase 7).
    CHECK(omr && std::abs(omr->material.roughness - RoughnessFromShininess(64.0f)) < 1e-5f);
    CHECK(omr && omr->material.specular == 0.1f);

    SECTION("Versão futura é recusada");
    Scene future;
    CHECK(!SceneSerializer::FromJson(R"({"format": "motor-scene", "version": 99})", future, assets, &error));

    return TestResult();
}
