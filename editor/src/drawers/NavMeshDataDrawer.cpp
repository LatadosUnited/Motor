// Desenhador do Inspector para o navmesh salvo da Superfície de navegação (motor::NavMeshData, ver
// motor/scene/NavMeshSurface.h): combo com os .mnav do projeto e o botão Gerar, que gera o navmesh da cena aberta e o
// grava (no arquivo já ligado, ou num novo em Assets/Navegacao).
#include "drawers/FieldDrawers.h"

#include <motor/ai/NavSystem.h>
#include <motor/assets/AssetLoaders.h>
#include <motor/core/Log.h>
#include <motor/scene/NavMeshSurface.h>

#include <imgui.h>

#include <algorithm>
#include <format>

namespace {

// A superfície dona deste campo (o desenhador só recebe o campo).
const motor::NavMeshSurface* SurfaceOf(const editor::EditorContext& ctx, const motor::NavMeshData& data) {
    for (motor::EntityId id : ctx.scene.EntitiesWith<motor::NavMeshSurface>()) {
        const motor::Entity* e = ctx.scene.Find(id);
        const motor::NavMeshSurface* surface = e ? e->Get<motor::NavMeshSurface>() : nullptr;
        if (surface && &surface->navMesh == &data) return surface;
    }
    return nullptr;
}

void Bake(editor::EditorContext& ctx, motor::NavMeshData& data) {
    const motor::NavMeshSurface* surface = SurfaceOf(ctx, data);
    if (!surface) return;
    std::filesystem::path file = data.file;
    if (file.empty()) file = ctx.NewAssetPath("Navegacao", "Navmesh", motor::kNavMeshFileExtension);
    if (file.empty()) {
        ctx.pendingError = "Abra ou crie um projeto para gerar o navmesh.";
        return;
    }

    motor::NavStats stats;
    const auto nav = motor::BuildSceneNavMesh(ctx.scene, *surface, &stats);
    if (!nav) {
        ctx.pendingError = std::format("O navmesh não foi gerado: {}.", stats.error);
        return;
    }
    std::string error;
    if (!nav->Save(file, &error)) {
        ctx.pendingError = std::format("Não foi possível gravar o navmesh: {}", error);
        return;
    }
    // Arquivo novo: o GUID sai do .meta criado agora. O campo mudar é um passo de desfazer, como qualquer edição.
    data.file = file;
    data.guid = ctx.assets.Database().GuidFor(file);
    data.path = ctx.assets.KeyForFile(file);
    motor::log::Info("Navegação: navmesh {} gerado com {} polígonos ({} triângulos) em {:.0f} ms", data.path,
                     stats.polygons, stats.triangles, stats.buildMs);
}

} // namespace

MOTOR_FIELD_DRAWER(NavMeshData) {
    auto& data = *static_cast<motor::NavMeshData*>(value);
    // Arquivo movido no painel Projeto (o .meta vai junto): acompanha o GUID, como o clipe de som.
    if (!data.guid.empty()) {
        if (const auto file = ctx.assets.Database().PathFor(data.guid); !file.empty() && file != data.file) data.file = file;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const char* buttonLabel = "Gerar";
    const float buttonWidth = ImGui::CalcTextSize(buttonLabel).x + style.FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - buttonWidth - style.ItemSpacing.x));
    const std::string current = data.path.empty() ? data.guid : data.path;
    const auto chosen = editor::AssetCombo(ctx, "##v", motor::AssetType::NavMesh, data.IsEmpty() ? nullptr : &current,
                                           "(gerado no Play)");
    if (!data.IsEmpty() && data.file.empty()) ImGui::SetItemTooltip("Arquivo não encontrado: %s", current.c_str());
    if (chosen) {
        if (chosen->empty()) {
            data = {};
        } else {
            data.file = ctx.assets.FileForKey(*chosen);
            data.guid = ctx.assets.Database().GuidFor(data.file);
            data.path = *chosen;
        }
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(ctx.playing);
    if (ImGui::Button(buttonLabel)) Bake(ctx, data);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip(ctx.playing ? "Pare o Play para gerar o navmesh"
                                      : "Gera o navmesh dos Colliders estáticos da cena e grava no arquivo (ou num novo "
                                        "em Assets/Navegacao). Gere de novo quando o cenário mudar");
}
