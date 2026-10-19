#include "motor/scene/ColliderComponent.h"

#include <nlohmann/json.hpp>

#include <iterator>
#include <string>

namespace motor {

namespace {
constexpr ColliderShape kShapes[] = {ColliderShape::Box, ColliderShape::Sphere, ColliderShape::Capsule,
                                     ColliderShape::Mesh, ColliderShape::Cylinder};
} // namespace

const char* ColliderShapeName(ColliderShape shape) {
    switch (shape) {
        case ColliderShape::Box: return "Box";
        case ColliderShape::Sphere: return "Sphere";
        case ColliderShape::Capsule: return "Capsule";
        case ColliderShape::Mesh: return "Mesh";
        case ColliderShape::Cylinder: return "Cylinder";
    }
    return "Box";
}

const char* ColliderShapeLabel(ColliderShape shape) {
    switch (shape) {
        case ColliderShape::Box: return "Caixa";
        case ColliderShape::Sphere: return "Esfera";
        case ColliderShape::Capsule: return "Cápsula";
        case ColliderShape::Mesh: return "Malha";
        case ColliderShape::Cylinder: return "Cilindro";
    }
    return "?";
}

CustomFieldType FieldTraits<ColliderShape>::Describe() {
    return {"ColliderShape",
            [](const void* a, const void* b) {
                return *static_cast<const ColliderShape*>(a) == *static_cast<const ColliderShape*>(b);
            },
            [](const void* v, const AssetLibrary&) {
                return nlohmann::json(ColliderShapeName(*static_cast<const ColliderShape*>(v))).dump();
            },
            [](std::string_view text, void* v, AssetLibrary&, std::string* error) {
                const auto json = nlohmann::json::parse(text, nullptr, false);
                auto& shape = *static_cast<ColliderShape*>(v);
                if (json.is_string()) {
                    for (ColliderShape s : kShapes) {
                        if (json.get<std::string>() == ColliderShapeName(s)) {
                            shape = s;
                            return true;
                        }
                    }
                } else if (json.is_number_integer()) {
                    const int n = json.get<int>();
                    if (n >= 0 && n < static_cast<int>(std::size(kShapes))) {
                        shape = kShapes[n];
                        return true;
                    }
                }
                if (error) *error = "esperado \"Box\", \"Sphere\", \"Capsule\", \"Cylinder\" ou \"Mesh\"";
                return false;
            }};
}

MOTOR_COMPONENT_MODULE(Collider) {
    registry.Register<ColliderComponent>("Collider", "Collider")
        // "shape", e não "type": no arquivo, "type" é o nome do tipo do componente.
        .Field("shape", "Forma", &ColliderComponent::shape)
        .Field("size", "Tamanho", &ColliderComponent::size,
               {.speed = 0.01f, .min = 0.001f, .max = 10000.0f, .format = "%.3f", .tooltip = "Caixa: tamanho total"})
        .Field("radius", "Raio", &ColliderComponent::radius,
               {.speed = 0.01f, .min = 0.001f, .max = 10000.0f, .format = "%.3f", .tooltip = "Esfera, cápsula e cilindro"})
        .Field("height", "Altura", &ColliderComponent::height,
               {.speed = 0.01f, .min = 0.001f, .max = 10000.0f, .format = "%.3f",
                .tooltip = "Cápsula (altura total, com as pontas) e cilindro, no eixo Y local"})
        .Field("center", "Centro", &ColliderComponent::center,
               {.speed = 0.01f, .tooltip = "Deslocamento da forma em relação à origem da entidade"})
        .Field("isTrigger", "Trigger", &ColliderComponent::isTrigger,
               {.tooltip = "Só detecta quem entra e sai (OnTriggerEnter/OnTriggerExit), sem empurrar"})
        .Field("friction", "Atrito", &ColliderComponent::friction,
               {.slider = true, .min = 0.0f, .max = 1.0f, .format = "%.2f"})
        .Field("restitution", "Elasticidade", &ColliderComponent::restitution,
               {.slider = true, .min = 0.0f, .max = 1.0f, .format = "%.2f",
                .tooltip = "0 = não quica, 1 = quica sem perder energia"})
        .Field("layer", "Camada", &ColliderComponent::layer,
               {.tooltip = "Camada de colisão: quem se choca com quem fica em Projeto > Camadas de colisão"});
}

} // namespace motor
