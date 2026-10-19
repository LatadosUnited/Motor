// Desenhador do Inspector para o grupo de volume da fonte de som (motor::AudioGroup, ver motor/scene/AudioSource.h).
#include "drawers/FieldDrawers.h"

#include <motor/scene/AudioSource.h>

#include <imgui.h>

MOTOR_FIELD_DRAWER(AudioGroup) {
    auto& group = *static_cast<motor::AudioGroup*>(value);
    constexpr motor::AudioGroup kGroups[] = {motor::AudioGroup::Effects, motor::AudioGroup::Music,
                                             motor::AudioGroup::Interface};
    if (ImGui::BeginCombo("##v", motor::AudioGroupLabel(group))) {
        for (motor::AudioGroup g : kGroups) {
            const bool selected = g == group;
            if (ImGui::Selectable(motor::AudioGroupLabel(g), selected)) group = g;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}
