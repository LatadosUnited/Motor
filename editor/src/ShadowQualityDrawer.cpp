// Desenhador do Inspector para a qualidade da sombra (motor::ShadowQuality, ver motor/scene/LightComponent.h).
#include "FieldDrawers.h"

#include <imgui.h>

MOTOR_FIELD_DRAWER(ShadowQuality) {
    auto& quality = *static_cast<motor::ShadowQuality*>(value);
    constexpr motor::ShadowQuality kQualities[] = {motor::ShadowQuality::Low, motor::ShadowQuality::Medium,
                                                   motor::ShadowQuality::High};
    if (ImGui::BeginCombo("##v", motor::ShadowQualityLabel(quality))) {
        for (motor::ShadowQuality q : kQualities) {
            const bool selected = q == quality;
            if (ImGui::Selectable(motor::ShadowQualityLabel(q), selected)) quality = q;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}
