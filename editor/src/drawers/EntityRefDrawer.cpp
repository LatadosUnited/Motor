// Desenhador do Inspector para campos EntityRef (motor::EntityRef, ver motor/scene/EntityRef.h): lista com as
// entidades da cena, que também aceita uma entidade arrastada da Hierarquia.
#include "drawers/FieldDrawers.h"

#include <motor/scene/EntityRef.h>
#include <motor/scene/Scene.h>

#include <imgui.h>

#include <format>
#include <string>

MOTOR_FIELD_DRAWER(EntityRef) {
    auto& ref = *static_cast<motor::EntityRef*>(value);
    const motor::Entity* target = ctx.scene.Find(ref.id);
    const std::string preview = ref.IsEmpty() ? std::string("(nenhuma)")
                                : target      ? target->name
                                              : std::format("(entidade #{} não existe)", ref.id);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##v", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable("(nenhuma)", ref.IsEmpty())) ref = {};
        for (const auto& e : ctx.scene.Entities()) {
            ImGui::PushID(static_cast<int>(e->id));
            if (ImGui::Selectable(e->name.c_str(), e->id == ref.id)) ref.id = e->id;
            ImGui::SameLine();
            ImGui::TextDisabled("#%u", e->id);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(editor::kEntityPayload)) {
            ref.id = *static_cast<const motor::EntityId*>(payload->Data);
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SetItemTooltip("Escolha na lista ou arraste uma entidade da Hierarquia");
}
