// Luzes (Fase 4): componente Luz, tipo da luz (FieldTraits), coleta para o renderer e conversão das cenas
// antigas, que tinham uma luz direcional fixa. Não usa OpenGL.
#include "TestUtil.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneSerializer.h>

#include <glm/geometric.hpp>

#include <cmath>
#include <string>

using namespace motor;

namespace {
bool Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    return glm::length(a - b) < eps;
}

bool Contains(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
}

const LightComponent* LightOf(const Scene& scene, std::string_view name) {
    for (const auto& e : scene.Entities()) {
        if (e->name == name) return e->Get<LightComponent>();
    }
    return nullptr;
}
} // namespace

int main() {
    AssetLibrary assets;
    std::string error;

    SECTION("Componente Luz registrado, com o tipo como campo próprio");
    const ComponentType* type = ComponentRegistry::Instance().Find("Light");
    CHECK(type && type->label == "Luz");
    CHECK(ComponentRegistry::TypeOf<LightComponent>() == type);
    const FieldInfo* typeField = type && !type->fields.empty() ? &type->fields[0] : nullptr;
    CHECK(typeField && typeField->key == "kind" && typeField->type == FieldType::Custom && typeField->custom &&
          typeField->custom->name == "LightType");

    SECTION("Rotação a partir de uma direção (-Z local aponta para ela)");
    for (const glm::vec3 dir : {glm::vec3(-0.5f, -1.0f, -0.35f), glm::vec3(0, -1, 0), glm::vec3(0, 1, 0),
                                glm::vec3(1, 0, 0), glm::vec3(0, 0, -1)}) {
        Transform t;
        t.rotation = RotationLookingAt(dir);
        const glm::vec3 forward = -glm::vec3(t.Matrix()[2]);
        CHECK(Near(forward, glm::normalize(dir)));
    }

    SECTION("CollectLights: posição e direção do mundo, só entidades ativas");
    Scene scene;
    Entity& poste = scene.CreateEntity("Poste");
    poste.transform.position = {5.0f, 0.0f, 0.0f};
    const EntityId posteId = poste.id;
    Entity& lampada = scene.CreateEntity("Lâmpada", posteId);
    lampada.transform.position = {0.0f, 3.0f, 0.0f};
    LightComponent& lc = lampada.Add<LightComponent>();
    lc.color = {1.0f, 0.8f, 0.5f};
    lc.intensity = 2.5f;
    lc.range = 7.0f;
    const EntityId lampadaId = lampada.id;

    Entity& holofote = scene.CreateEntity("Holofote");
    holofote.transform.position = {0.0f, 4.0f, 0.0f};
    holofote.transform.rotation = RotationLookingAt({0.0f, -1.0f, 0.0f});
    LightComponent& spot = holofote.Add<LightComponent>();
    spot.type = LightType::Spot;
    spot.innerAngle = 15.0f;
    spot.outerAngle = 30.0f;

    Entity& apagada = scene.CreateEntity("Apagada");
    apagada.Add<LightComponent>();
    apagada.active = false;

    std::vector<Light> lights = scene.CollectLights();
    CHECK(lights.size() == 2);
    if (lights.size() == 2) {
        CHECK(lights[0].type == LightType::Point && Near(lights[0].position, {5.0f, 3.0f, 0.0f}));
        CHECK(lights[0].intensity == 2.5f && lights[0].range == 7.0f && lights[0].color == glm::vec3(1.0f, 0.8f, 0.5f));
        CHECK(lights[1].type == LightType::Spot && Near(lights[1].direction, {0.0f, -1.0f, 0.0f}));
        CHECK(lights[1].innerAngle == 15.0f && lights[1].outerAngle == 30.0f);
    }
    scene.Find(posteId)->active = false;  // pai inativo apaga a lâmpada
    CHECK(scene.CollectLights().size() == 1);
    scene.Find(posteId)->active = true;

    SECTION("Desfazer enxerga mudanças nas luzes e no ambiente");
    {
        Scene copy = scene;
        CHECK(copy == scene);
        copy.Find(lampadaId)->Get<LightComponent>()->type = LightType::Spot;
        CHECK(!(copy == scene));
        copy = scene;
        copy.environment.ambient = {0.0f, 0.0f, 0.05f};
        CHECK(!(copy == scene));
    }

    SECTION("Salvar e carregar luzes (versão 4)");
    scene.environment.ambient = {0.02f, 0.03f, 0.06f};
    const std::string text = SceneSerializer::ToJson(scene, assets);
    CHECK(Contains(text, "\"version\": 5"));
    CHECK(Contains(text, "\"kind\": \"Spot\"") && Contains(text, "\"kind\": \"Point\"") && Contains(text, "\"type\": \"Light\""));
    CHECK(Contains(text, "\"ambient\": [0.02, 0.03, 0.06]"));
    CHECK(!Contains(text, "\"light\""));
    Scene loaded;
    CHECK(SceneSerializer::FromJson(text, loaded, assets, &error));
    CHECK(loaded.Entities().size() == scene.Entities().size());  // cena nova: nada convertido
    CHECK(loaded == scene);
    CHECK(SceneSerializer::ToJson(loaded, assets) == text);

    SECTION("Tipo da luz: número também vale; valor desconhecido mantém o padrão");
    {
        Scene s;
        CHECK(SceneSerializer::FromJson(R"({"format": "motor-scene", "version": 4, "entities": [
            {"id": 1, "name": "N", "components": [{"type": "Light", "kind": 2}]},
            {"id": 2, "name": "D", "components": [{"type": "Light", "kind": "Directional", "range": 3}]},
            {"id": 3, "name": "X", "components": [{"type": "Light", "kind": "Laser"}]},
            {"id": 4, "name": "Y", "components": [{"type": "Light", "kind": 9}]}]})",
                                        s, assets, &error));
        const LightComponent *n = LightOf(s, "N"), *d = LightOf(s, "D"), *x = LightOf(s, "X"), *y = LightOf(s, "Y");
        CHECK(n && n->type == LightType::Spot);
        CHECK(d && d->type == LightType::Directional && d->range == 3.0f);
        CHECK(x && x->type == LightType::Point);
        CHECK(y && y->type == LightType::Point);
    }

    SECTION("Cena da versão 3: a luz da cena vira uma entidade com o componente Luz");
    {
        const char* v3 = R"({"format": "motor-scene", "version": 3,
            "environment": {"skyColor": [0.1, 0.1, 0.2], "fogDensity": 0.02},
            "light": {"direction": [0, -2, -2], "color": [0.9, 0.8, 0.7], "intensity": 0.4,
                      "ambient": [0.05, 0.05, 0.1]},
            "entities": [{"id": 7, "name": "Chão"}, {"id": 9, "name": "Filho", "parent": 7}]})";
        Scene old;
        CHECK(SceneSerializer::FromJson(v3, old, assets, &error));
        CHECK(old.Entities().size() == 3);
        const Entity* sun = old.Entities().size() == 3 ? old.Entities().back().get() : nullptr;
        const LightComponent* light = sun ? sun->Get<LightComponent>() : nullptr;
        CHECK(sun && sun->name == "Luz direcional" && sun->parent == kNullEntity);
        CHECK(light && light->type == LightType::Directional);
        CHECK(light && light->color == glm::vec3(0.9f, 0.8f, 0.7f) && light->intensity == 0.4f);
        CHECK(old.environment.ambient == glm::vec3(0.05f, 0.05f, 0.1f));
        CHECK(old.environment.fogDensity == 0.02f);
        const std::vector<Light> converted = old.CollectLights();
        CHECK(converted.size() == 1 &&
              Near(converted[0].direction, glm::normalize(glm::vec3(0.0f, -2.0f, -2.0f))));
        // A hierarquia do arquivo continua igual (a entidade nova entra no fim).
        CHECK(old.Entities()[1]->parent == old.Entities()[0]->id);

        // Salva já no formato novo, e abrir de novo não converte outra vez.
        const std::string saved = SceneSerializer::ToJson(old, assets);
        CHECK(Contains(saved, "\"version\": 5") && !Contains(saved, "\"light\""));
        Scene again;
        CHECK(SceneSerializer::FromJson(saved, again, assets, &error));
        CHECK(again.Entities().size() == 3);
        CHECK(again == old);
        CHECK(SceneSerializer::ToJson(again, assets) == saved);
    }

    SECTION("Cena antiga sem \"light\": luz direcional com os valores padrão de antes");
    {
        Scene old;
        CHECK(SceneSerializer::FromJson(R"({"format": "motor-scene", "version": 1, "entities": []})", old, assets,
                                        &error));
        const std::vector<Light> converted = old.CollectLights();
        CHECK(converted.size() == 1 && converted[0].type == LightType::Directional);
        CHECK(converted.size() == 1 && Near(converted[0].direction, glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f))));
        CHECK(converted.size() == 1 && converted[0].intensity == 1.0f);
        CHECK(old.environment.ambient == Environment{}.ambient);
    }

    SECTION("Cena da versão 4 sem luzes continua sem luzes");
    {
        Scene empty;
        CHECK(SceneSerializer::FromJson(R"({"format": "motor-scene", "version": 4, "entities": []})", empty, assets,
                                        &error));
        CHECK(empty.Entities().empty() && empty.CollectLights().empty());
    }

    return TestResult();
}
