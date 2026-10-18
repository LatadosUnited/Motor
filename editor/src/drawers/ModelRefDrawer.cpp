// Desenhador do Inspector para o modelo glTF do Animator (std::shared_ptr<motor::Model>, ver
// motor/scene/Animator.h). Mostra também os clipes do modelo (o nome vai no campo "Clipe inicial").
#include "drawers/FieldDrawers.h"

#include <motor/assets/Model.h>

#include <imgui.h>

#include <format>

MOTOR_FIELD_DRAWER(ModelRef) {
    auto& model = *static_cast<std::shared_ptr<motor::Model>*>(value);
    std::string currentKey;
    if (model) {
        const auto ref = ctx.assets.RefOf(model.get());
        currentKey = ref && !ref->name.empty() ? ref->name : "(arquivo não encontrado)";
    }
    const auto chosen = editor::AssetCombo(ctx, "##v", motor::AssetType::Model, model ? &currentKey : nullptr, "(nenhum)");
    if (model && !model->clips.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        std::string names;
        for (const auto& clip : model->clips) names += std::format("\n  {} ({:.2f} s)", clip->name, clip->duration);
        ImGui::SetTooltip("%s\nClipes:%s", currentKey.c_str(), names.c_str());
    }
    if (!chosen) return;
    if (chosen->empty()) {
        model = nullptr;
        return;
    }
    std::string error;
    if (auto loaded = ctx.assets.GetModel(*chosen, &error)) {
        model = std::move(loaded);
    } else {
        ctx.pendingError = std::move(error);
    }
}
