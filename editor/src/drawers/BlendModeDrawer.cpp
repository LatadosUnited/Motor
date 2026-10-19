// Desenhador do Inspector para o modo de alfa do material (motor::BlendMode, ver motor/renderer/Material.h).
#include "drawers/FieldDrawers.h"

#include <motor/renderer/Material.h>

#include <imgui.h>

MOTOR_FIELD_DRAWER(BlendMode) {
    auto& mode = *static_cast<motor::BlendMode*>(value);
    constexpr motor::BlendMode kModes[] = {motor::BlendMode::Opaque, motor::BlendMode::Cutout,
                                           motor::BlendMode::Transparent};
    if (ImGui::BeginCombo("##v", motor::BlendModeLabel(mode))) {
        for (motor::BlendMode m : kModes) {
            const bool selected = m == mode;
            if (ImGui::Selectable(motor::BlendModeLabel(m), selected)) mode = m;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}
