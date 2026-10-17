#pragma once

#include "motor/renderer/Light.h"
#include "motor/scene/Component.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace motor {

// Luz da cena. Posição e direção vêm da entidade: a luz aponta para o -Z local (como a Câmera), então
// direcional e spot se giram com o gizmo de rotação. Várias por cena (até kMaxLights por passada).
struct LightComponent {
    LightType type = LightType::Point;
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 10.0f;       // pontual e spot
    float innerAngle = 25.0f;  // spot: meio ângulo (graus) com luz total
    float outerAngle = 35.0f;  // spot: meio ângulo (graus) onde a luz acaba
    bool castShadows = true;   // projeta sombra (direcional: cascatas; pontual/spot: cubo, até kMaxPointShadows)
    ShadowQuality shadowQuality = ShadowQuality::Medium;
};

// Na cena o tipo é gravado como texto ("Directional", "Point", "Spot"); números também são aceitos.
template <> struct FieldTraits<LightType> {
    static CustomFieldType Describe();
};

// Gravada como texto ("Low", "Medium", "High"); números também são aceitos.
template <> struct FieldTraits<ShadowQuality> {
    static CustomFieldType Describe();
};

const char* LightTypeName(LightType type);   // identificador gravado na cena
const char* LightTypeLabel(LightType type);  // nome no editor ("Pontual")
const char* ShadowQualityName(ShadowQuality quality);   // "Low", "Medium", "High"
const char* ShadowQualityLabel(ShadowQuality quality);  // nome no editor ("Média (2048)")

// Luz do renderer a partir do componente e da matriz de mundo da entidade.
Light MakeLight(const LightComponent& component, const glm::mat4& world);

// Rotação que faz o -Z local apontar para `direction` (para posicionar luzes e câmeras por direção).
glm::quat RotationLookingAt(const glm::vec3& direction);

} // namespace motor
