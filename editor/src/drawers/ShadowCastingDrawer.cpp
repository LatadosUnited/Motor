// Desenhador do Inspector para a sombra do Mesh Renderer (motor::ShadowCasting, ver motor/renderer/Material.h).
#include "drawers/FieldDrawers.h"

#include <motor/renderer/Material.h>

#include <imgui.h>

MOTOR_FIELD_DRAWER(ShadowCasting) {
    auto& mode = *static_cast<motor::ShadowCasting*>(value);
    constexpr motor::ShadowCasting kModes[] = {motor::ShadowCasting::On, motor::ShadowCasting::Off,
                                               motor::ShadowCasting::ShadowsOnly};
    if (ImGui::BeginCombo("##v", motor::ShadowCastingLabel(mode))) {
        for (motor::ShadowCasting m : kModes) {
            const bool selected = m == mode;
            if (ImGui::Selectable(motor::ShadowCastingLabel(m), selected)) mode = m;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}
