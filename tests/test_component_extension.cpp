// Pontos de extensão dos componentes (Component.h): módulos registrados com MOTOR_COMPONENT_MODULE e tipos
// de campo próprios com FieldTraits<T>, sem tocar em arquivos centrais da engine.
#include "TestUtil.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/scene/Component.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneSerializer.h>

#include <charconv>
#include <string>

using namespace motor;

namespace {
// Tipo de campo "de fora": um intervalo inteiro, gravado como [min, max].
struct Faixa {
    int min = 0;
    int max = 10;
    bool operator==(const Faixa&) const = default;
};

// Componente de um "módulo" que não existe na engine (como seriam os de física ou áudio).
struct Sensor {
    float raio = 2.0f;
    Faixa faixa;
};

int ParseInt(std::string_view text, int fallback) {
    int value = fallback;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}
} // namespace

namespace motor {
template <> struct FieldTraits<Faixa> {
    static CustomFieldType Describe() {
        return {"Faixa",
                [](const void* a, const void* b) { return *static_cast<const Faixa*>(a) == *static_cast<const Faixa*>(b); },
                [](const void* v, const AssetLibrary&) {
                    const auto& f = *static_cast<const Faixa*>(v);
                    return "[" + std::to_string(f.min) + ", " + std::to_string(f.max) + "]";
                },
                [](std::string_view json, void* v, AssetLibrary&, std::string* error) {
                    // "[a, b]" (sem biblioteca de JSON no teste: basta para o formato gravado acima).
                    const size_t open = json.find('['), comma = json.find(','), close = json.find(']');
                    if (open == std::string_view::npos || comma == std::string_view::npos || close == std::string_view::npos) {
                        if (error) *error = "esperado [min, max]";
                        return false;
                    }
                    auto& f = *static_cast<Faixa*>(v);
                    f.min = ParseInt(json.substr(open + 1, comma - open - 1), f.min);
                    const std::string_view second = json.substr(comma + 1, close - comma - 1);
                    f.max = ParseInt(second.substr(second.find_first_not_of(' ')), f.max);
                    return true;
                }};
    }
};
} // namespace motor

MOTOR_COMPONENT_MODULE(Sensores) {
    registry.Register<Sensor>("Sensor", "Sensor")
        .Field("raio", "Raio", &Sensor::raio)
        .Field("faixa", "Faixa", &Sensor::faixa);
}

int main() {
    SECTION("Módulos: os da engine e um de fora, em ordem alfabética do nome do módulo");
    const auto& registry = ComponentRegistry::Instance();
    const ComponentType* camera = registry.Find("Camera");
    const ComponentType* mesh = registry.Find("MeshRenderer");
    const ComponentType* sensor = registry.Find("Sensor");
    CHECK(camera && mesh && sensor);
    CHECK(mesh && mesh->fields.size() == 20);  // malha, material (asset), os 16 do material próprio (PBR, Fase 7; alfa, Fase 18), a sombra e o estático (Fase 28)
    CHECK(ComponentRegistry::TypeOf<CameraComponent>() == camera);
    CHECK(ComponentRegistry::TypeOf<Sensor>() == sensor);
    // Módulos "Camera" < "MeshRenderer" < "Sensores" (outros módulos da engine podem ficar no meio).
    const auto& types = registry.Types();
    auto indexOf = [&](const ComponentType* type) {
        for (size_t i = 0; i < types.size(); ++i) {
            if (types[i].get() == type) return i;
        }
        return types.size();
    };
    CHECK(types.size() >= 3 && types[0].get() == camera && indexOf(camera) < indexOf(mesh) &&
          indexOf(mesh) < indexOf(sensor) && indexOf(sensor) < types.size());

    SECTION("Tipo de campo próprio (FieldTraits)");
    CHECK(sensor && sensor->fields.size() == 2);
    const FieldInfo* faixa = sensor ? &sensor->fields[1] : nullptr;
    CHECK(faixa && faixa->type == FieldType::Custom && faixa->custom && faixa->custom->name == "Faixa");

    Scene scene;
    Entity& e = scene.CreateEntity("Porta");
    Sensor& s = e.Add<Sensor>();
    s.faixa = {3, 7};
    s.raio = 4.5f;

    Scene copy = scene;
    CHECK(copy == scene);
    copy.Find(e.id)->Get<Sensor>()->faixa.max = 8;
    CHECK(!(copy == scene));  // a comparação (desfazer) usa o equal do tipo

    SECTION("Cena: salvar e carregar o campo próprio");
    AssetLibrary assets;
    const std::string text = SceneSerializer::ToJson(scene, assets);
    CHECK(text.find("\"faixa\": [") != std::string::npos);
    Scene loaded;
    std::string error;
    CHECK(SceneSerializer::FromJson(text, loaded, assets, &error));
    const Entity* le = loaded.Entities().empty() ? nullptr : loaded.Entities().front().get();
    const Sensor* ls = le ? le->Get<Sensor>() : nullptr;
    CHECK(ls && ls->faixa == (Faixa{3, 7}) && ls->raio == 4.5f);
    CHECK(loaded == scene);
    CHECK(SceneSerializer::ToJson(loaded, assets) == text);  // ida e volta estável

    SECTION("JSON inesperado no campo próprio: valor padrão");
    const char* odd = R"({"format": "motor-scene", "version": 3, "entities": [
        {"id": 1, "name": "A", "components": [{"type": "Sensor", "faixa": "texto"}]}]})";
    Scene p;
    CHECK(SceneSerializer::FromJson(odd, p, assets, &error));
    const Entity* pe = p.Entities().empty() ? nullptr : p.Entities().front().get();
    CHECK(pe && pe->Get<Sensor>() && pe->Get<Sensor>()->faixa == Faixa{});

    return TestResult();
}
