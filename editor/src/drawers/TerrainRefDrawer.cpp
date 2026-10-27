// Desenhador do Inspector para o arquivo do Terreno (motor::TerrainRef, ver motor/scene/Terrain.h): combo com os
// .mterrain do projeto e, embaixo, o tamanho do terreno ligado.
#include "drawers/FieldDrawers.h"

#include <motor/assets/AssetLoaders.h>
#include <motor/scene/Terrain.h>

#include <imgui.h>

MOTOR_FIELD_DRAWER(TerrainRef) {
    auto& ref = *static_cast<motor::TerrainRef*>(value);
    // Arquivo movido no painel Projeto (o .meta e a pasta dos blocos vão junto): acompanha o GUID.
    if (!ref.guid.empty()) {
        if (const auto file = ctx.assets.Database().PathFor(ref.guid); !file.empty() && file != ref.file) ref.file = file;
    }

    const std::string current = ref.path.empty() ? ref.guid : ref.path;
    ImGui::SetNextItemWidth(-1.0f);
    const auto chosen = editor::AssetCombo(ctx, "##v", motor::AssetType::Terrain, ref.IsEmpty() ? nullptr : &current,
                                           "(nenhum)");
    if (!ref.IsEmpty() && ref.file.empty()) ImGui::SetItemTooltip("Arquivo não encontrado: %s", current.c_str());
    if (chosen) {
        if (chosen->empty()) {
            ref = {};
        } else {
            ref.file = ctx.assets.FileForKey(*chosen);
            ref.guid = ctx.assets.Database().GuidFor(ref.file);
            ref.path = *chosen;
        }
    }
    if (const motor::TerrainData* data = ref.Data()) {
        ImGui::TextDisabled("%zu blocos de %.0f m, %d x %d amostras%s", data->BlockCount(), data->BlockSize(),
                            data->Resolution(), data->Resolution(), data->IsDirty() ? " (não salvo)" : "");
    }
}
