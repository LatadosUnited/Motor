// Desenhador do Inspector para campos PrefabRef (motor::PrefabRef, ver motor/scene/PrefabInstance.h): combo
// com os .mprefab do projeto, que também aceita o arquivo arrastado do painel Projeto.
#include "drawers/FieldDrawers.h"

#include <motor/scene/PrefabInstance.h>

#include <imgui.h>

MOTOR_FIELD_DRAWER(PrefabRef) {
    auto& ref = *static_cast<motor::PrefabRef*>(value);
    // Arquivo movido no painel Projeto (o .meta vai junto): acompanha o GUID.
    if (!ref.guid.empty()) {
        if (const auto file = ctx.assets.Database().PathFor(ref.guid); !file.empty() && file != ref.file) {
            ref.file = file;
        }
    }
    const std::string current = ref.path.empty() ? ref.guid : ref.path;
    const auto chosen = editor::AssetCombo(ctx, "##v", motor::AssetType::Prefab, ref.IsEmpty() ? nullptr : &current,
                                           "(nenhum)");
    if (!ref.IsEmpty() && ref.file.empty()) ImGui::SetItemTooltip("Arquivo não encontrado: %s", current.c_str());
    if (!chosen) return;
    if (chosen->empty()) {
        ref = {};
        return;
    }
    ref.file = ctx.assets.FileForKey(*chosen);
    ref.guid = ctx.assets.Database().GuidFor(ref.file);
    ref.path = *chosen;
}
