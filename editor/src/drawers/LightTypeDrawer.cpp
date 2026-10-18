// Desenhador do Inspector para o tipo da luz (motor::LightType, ver motor/scene/LightComponent.h).
#include "drawers/FieldDrawers.h"

#include <imgui.h>

MOTOR_FIELD_DRAWER(LightType) {
    auto& type = *static_cast<motor::LightType*>(value);
    constexpr motor::LightType kTypes[] = {motor::LightType::Directional, motor::LightType::Point,
                                           motor::LightType::Spot};
    if (ImGui::BeginCombo("##v", motor::LightTypeLabel(type))) {
        for (motor::LightType t : kTypes) {
            const bool selected = t == type;
            if (ImGui::Selectable(motor::LightTypeLabel(t), selected)) type = t;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}
