// Desenhador do Inspector para o arquivo do Terreno (motor::TerrainRef, ver motor/scene/Terrain.h): combo com os
// .mterrain do projeto, embaixo o tamanho do terreno ligado e os pincéis (usados no Viewport, TerrainBrushTool).
#include "drawers/FieldDrawers.h"

#include <motor/assets/AssetLoaders.h>
#include <motor/scene/Terrain.h>

#include <imgui.h>

#include <utility>

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
    const motor::TerrainData* data = ref.Data();
    if (!data) return;
    ImGui::TextDisabled("%zu blocos de %.0f m, %d x %d amostras%s", data->BlockCount(), data->BlockSize(),
                        data->Resolution(), data->Resolution(), data->IsDirty() ? " (não salvo)" : "");

    // Pincel: o mesmo para todos os terrenos; vale para o selecionado.
    auto& state = ctx.terrainBrush;
    ImGui::BeginDisabled(ctx.playing);
    static constexpr std::pair<const char*, motor::TerrainBrushMode> kModes[] = {
        {"Subir", motor::TerrainBrushMode::Raise}, {"Baixar", motor::TerrainBrushMode::Lower},
        {"Suavizar", motor::TerrainBrushMode::Smooth}, {"Nivelar", motor::TerrainBrushMode::Flatten}};
    if (ImGui::RadioButton("Selecionar", !state.active)) state.active = false;
    ImGui::SetItemTooltip("O clique no Viewport seleciona (Esc no Viewport volta para cá)");
    for (const auto& [label, mode] : kModes) {
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(label).x + ImGui::GetFrameHeight() * 1.5f) ImGui::NewLine();
        if (ImGui::RadioButton(label, state.active && state.brush.mode == mode)) {
            state.active = true;
            state.brush.mode = mode;
        }
    }
    if (state.active) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##raio", &state.brush.radius, motor::TerrainBrush::kMinRadius, 64.0f, "Raio %.1f m",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##forca", &state.brush.strength, 0.01f, 1.0f, "Força %.2f");
        ImGui::TextDisabled(state.brush.mode == motor::TerrainBrushMode::Flatten
                                ? "Arraste no Viewport: nivela na altura de onde começou"
                                : "Clique e arraste no terreno, no Viewport");
    }
    ImGui::EndDisabled();
}
