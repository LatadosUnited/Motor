#include "motor/scene/LightComponent.h"

#include <nlohmann/json.hpp>

#include <glm/geometric.hpp>

#include <cmath>
#include <string>

namespace motor {

namespace {
constexpr LightType kLightTypes[] = {LightType::Directional, LightType::Point, LightType::Spot};
} // namespace

const char* LightTypeName(LightType type) {
    switch (type) {
        case LightType::Directional: return "Directional";
        case LightType::Point: return "Point";
        case LightType::Spot: return "Spot";
    }
    return "Point";
}

const char* LightTypeLabel(LightType type) {
    switch (type) {
        case LightType::Directional: return "Direcional";
        case LightType::Point: return "Pontual";
        case LightType::Spot: return "Spot";
    }
    return "?";
}

CustomFieldType FieldTraits<LightType>::Describe() {
    return {"LightType",
            [](const void* a, const void* b) { return *static_cast<const LightType*>(a) == *static_cast<const LightType*>(b); },
            [](const void* v, const AssetLibrary&) {
                return nlohmann::json(LightTypeName(*static_cast<const LightType*>(v))).dump();
            },
            [](std::string_view text, void* v, AssetLibrary&) {
                const auto json = nlohmann::json::parse(text, nullptr, false);
                auto& type = *static_cast<LightType*>(v);
                if (json.is_string()) {
                    for (LightType t : kLightTypes) {
                        if (json.get<std::string>() == LightTypeName(t)) type = t;
                    }
                } else if (json.is_number_integer()) {
                    const int n = json.get<int>();
                    if (n >= 0 && n < static_cast<int>(std::size(kLightTypes))) type = kLightTypes[n];
                }
                // Outro valor: fica como está (leitura tolerante).
            }};
}

Light MakeLight(const LightComponent& component, const glm::mat4& world) {
    Light light;
    light.type = component.type;
    light.position = glm::vec3(world[3]);
    const glm::vec3 forward = -glm::vec3(world[2]);
    light.direction = glm::length(forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(0.0f, -1.0f, 0.0f);
    light.color = component.color;
    light.intensity = component.intensity;
    light.range = component.range;
    light.innerAngle = component.innerAngle;
    light.outerAngle = component.outerAngle;
    return light;
}

glm::quat RotationLookingAt(const glm::vec3& direction) {
    if (glm::length(direction) < 1e-6f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 dir = glm::normalize(direction);
    // quatLookAt não aceita "para cima" paralelo à direção: luz apontando reto para baixo usa o -Z como referência.
    const glm::vec3 up = std::abs(dir.y) > 0.999f ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(glm::quatLookAt(dir, up));
}

MOTOR_COMPONENT_MODULE(Light) {
    registry.Register<LightComponent>("Light", "Luz")
        // "kind", e não "type": no arquivo, "type" é o nome do tipo do componente.
        .Field("kind", "Tipo", &LightComponent::type)
        .Field("color", "Cor", &LightComponent::color, {.color = true})
        .Field("intensity", "Intensidade", &LightComponent::intensity,
               {.speed = 0.01f, .min = 0.0f, .max = 100.0f, .format = "%.2f"})
        .Field("range", "Alcance", &LightComponent::range,
               {.speed = 0.05f, .min = 0.1f, .max = 1000.0f, .format = "%.1f m",
                .tooltip = "Pontual e spot: distância em que a luz chega a zero"})
        .Field("innerAngle", "Ângulo interno", &LightComponent::innerAngle,
               {.slider = true, .min = 0.0f, .max = 89.0f, .format = "%.0f°",
                .tooltip = "Spot: meio ângulo do cone com luz total"})
        .Field("outerAngle", "Ângulo externo", &LightComponent::outerAngle,
               {.slider = true, .min = 1.0f, .max = 89.0f, .format = "%.0f°",
                .tooltip = "Spot: meio ângulo do cone onde a luz acaba"});
}

} // namespace motor
