// Desenhador do Inspector para o navmesh salvo da Superfície de navegação (motor::NavMeshData, ver
// motor/scene/NavMeshSurface.h): combo com os .mnav do projeto e o botão Gerar (NavMeshBake.h), que gera o navmesh
// da cena aberta e o grava (no arquivo já ligado, ou num novo em Assets/Navegacao).
#include "drawers/FieldDrawers.h"

#include "NavMeshBake.h"

#include <motor/assets/AssetLoaders.h>
#include <motor/scene/NavMeshSurface.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>

namespace {

// A superfície dona deste campo (o desenhador só recebe o campo).
motor::NavMeshSurface* SurfaceOf(editor::EditorContext& ctx, const motor::NavMeshData& data) {
    for (motor::EntityId id : ctx.scene.EntitiesWith<motor::NavMeshSurface>()) {
        motor::Entity* e = ctx.scene.Find(id);
        motor::NavMeshSurface* surface = e ? e->Get<motor::NavMeshSurface>() : nullptr;
        if (surface && &surface->navMesh == &data) return surface;
    }
    return nullptr;
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
    if (ImGui::Button(buttonLabel)) {
        if (motor::NavMeshSurface* surface = SurfaceOf(ctx, data)) {
            const editor::NavBakeResult result = editor::BakeNavMesh(ctx, *surface);
            if (!result.ok) {
                std::string message = result.error;
                if (!message.empty()) message[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(message[0])));
                ctx.pendingError = message + ".";
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip(ctx.playing ? "Pare o Play para gerar o navmesh"
                                      : "Gera o navmesh dos Colliders estáticos da cena e grava no arquivo (ou num novo "
                                        "em Assets/Navegacao). Gere de novo quando o cenário mudar");
}
