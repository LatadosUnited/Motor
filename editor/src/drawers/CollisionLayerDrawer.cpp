// Desenhador do Inspector para a camada de colisão (motor::CollisionLayer, ver motor/project/CollisionLayers.h):
// lista as camadas do projeto pelo nome (as sem nome só aparecem se já estiverem em uso).
#include "drawers/FieldDrawers.h"

#include <motor/physics/PhysicsSystem.h>

#include <imgui.h>

#include <format>

MOTOR_FIELD_DRAWER(CollisionLayer) {
    auto& layer = *static_cast<motor::CollisionLayer*>(value);
    const motor::CollisionLayers& layers = motor::PhysicsSettings::Global().layers;
    const std::string current = std::format("{}: {}", layer.index, layers.Label(layer.index));
    if (ImGui::BeginCombo("##v", current.c_str())) {
        for (int i = 0; i < motor::CollisionLayers::kCount; ++i) {
            const bool selected = i == layer.index;
            if (layers.names[static_cast<size_t>(i)].empty() && !selected) continue;
            const std::string label = std::format("{}: {}", i, layers.Label(i));
            if (ImGui::Selectable(label.c_str(), selected)) layer.index = static_cast<uint8_t>(i);
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::Separator();
        ImGui::TextDisabled("Nomes e quem se choca: Arquivo > Camadas de colisão");
        ImGui::EndCombo();
    }
}
