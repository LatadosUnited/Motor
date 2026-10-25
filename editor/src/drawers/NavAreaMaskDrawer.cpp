// Desenhador do Inspector para as áreas por onde um agente anda (motor::NavAreaMask, ver motor/scene/NavMeshArea.h):
// um combo com uma caixa de marcar por área. Os nomes vêm das Áreas de navegação da cena aberta; as áreas que a cena
// não usa só aparecem se estiverem desmarcadas (para dar para marcar de volta).
#include "drawers/FieldDrawers.h"

#include <motor/scene/NavMeshArea.h>

#include <imgui.h>

#include <array>
#include <bit>
#include <format>
#include <string>

MOTOR_FIELD_DRAWER(NavAreaMask) {
    auto& mask = *static_cast<motor::NavAreaMask*>(value);
    std::array<std::string, motor::kNavAreaCount> names;
    names[0] = "Chão comum";
    for (motor::EntityId id : ctx.scene.EntitiesWith<motor::NavMeshArea>()) {
        const motor::NavMeshArea& a = *ctx.scene.Find(id)->Get<motor::NavMeshArea>();
        if (a.area > 0 && a.area < motor::kNavAreaCount && names[static_cast<size_t>(a.area)].empty()) {
            names[static_cast<size_t>(a.area)] = a.name.empty() ? std::format("Área {}", a.area) : a.name;
        }
    }

    const int allowed = std::popcount(static_cast<unsigned>(mask.bits));
    const std::string preview = mask.bits == motor::NavAreaMask{}.bits ? std::string("Todas")
                                : allowed == 0 ? std::string("Nenhuma")
                                               : std::format("{} de {}", allowed, motor::kNavAreaCount);
    if (ImGui::BeginCombo("##v", preview.c_str())) {
        for (int i = 0; i < motor::kNavAreaCount; ++i) {
            const bool on = mask.Allows(i);
            if (names[static_cast<size_t>(i)].empty() && on) continue;  // área que a cena não usa e já marcada
            const std::string& name = names[static_cast<size_t>(i)];
            const std::string label = std::format("{}: {}", i, name.empty() ? std::string("(sem uso na cena)") : name);
            bool check = on;
            if (ImGui::Checkbox(label.c_str(), &check)) {
                mask.bits = static_cast<uint16_t>(check ? (mask.bits | (1u << i)) : (mask.bits & ~(1u << i)));
            }
        }
        ImGui::Separator();
        if (ImGui::SmallButton("Todas")) mask.bits = motor::NavAreaMask{}.bits;
        ImGui::TextDisabled("Áreas: componente Área de navegação");
        ImGui::EndCombo();
    }
}
