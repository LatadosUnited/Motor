// Desenhador do Inspector para o asset de material do Mesh Renderer (std::shared_ptr<motor::Material>,
// ver motor/scene/MeshRenderer.h). Os campos do material escolhido são desenhados pelo InspectorPanel.
#include "drawers/FieldDrawers.h"

#include <imgui.h>

MOTOR_FIELD_DRAWER(MaterialRef) {
    auto& material = *static_cast<std::shared_ptr<motor::Material>*>(value);
    std::string currentKey;
    if (material) {
        const auto ref = ctx.assets.RefOf(material.get());
        currentKey = ref && !ref->name.empty() ? ref->name : "(arquivo não encontrado)";
    }
    const auto chosen = editor::AssetCombo(ctx, "##v", motor::AssetType::Material, material ? &currentKey : nullptr,
                                           "(próprio)");
    if (!chosen) return;
    if (chosen->empty()) {
        material = nullptr;  // volta ao material próprio do componente
        return;
    }
    std::string error;
    if (auto loaded = ctx.assets.GetMaterial(*chosen, &error)) {
        material = std::move(loaded);
    } else {
        ctx.pendingError = std::move(error);
    }
}
