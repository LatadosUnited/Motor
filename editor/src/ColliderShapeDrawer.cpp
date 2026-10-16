// Desenhador do Inspector para a forma do collider (motor::ColliderShape, ver motor/scene/ColliderComponent.h).
#include "FieldDrawers.h"

#include <imgui.h>

MOTOR_FIELD_DRAWER(ColliderShape) {
    auto& shape = *static_cast<motor::ColliderShape*>(value);
    constexpr motor::ColliderShape kShapes[] = {motor::ColliderShape::Box, motor::ColliderShape::Sphere,
                                                motor::ColliderShape::Capsule, motor::ColliderShape::Mesh};
    if (ImGui::BeginCombo("##v", motor::ColliderShapeLabel(shape))) {
        for (motor::ColliderShape s : kShapes) {
            const bool selected = s == shape;
            if (ImGui::Selectable(motor::ColliderShapeLabel(s), selected)) shape = s;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (shape == motor::ColliderShape::Mesh) {
        ImGui::SetItemTooltip("Usa a malha do Mesh Renderer. Malha de arquivo (OBJ) vira malha estática; com "
                              "Rigidbody, o casco convexo. Malhas embutidas usam a caixa dos limites.");
    }
}
