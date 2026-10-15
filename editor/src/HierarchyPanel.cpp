#include "EditorContext.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>

namespace editor {

namespace {
constexpr const char* kEntityPayload = "MOTOR_ENTITY";

void AcceptEntityDrop(EditorContext& ctx, EntityId newParent) {
    if (!ImGui::BeginDragDropTarget()) return;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload)) {
        const EntityId dragged = *static_cast<const EntityId*>(payload->Data);
        ctx.scene.SetParent(dragged, newParent);  // recusa ciclos sozinho
    }
    ImGui::EndDragDropTarget();
}

void DrawEntityNode(EditorContext& ctx, EntityId id) {
    motor::Entity* e = ctx.scene.Find(id);
    if (!e) return;

    const std::vector<EntityId> children = ctx.scene.Children(id);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (ctx.selected == id) flags |= ImGuiTreeNodeFlags_Selected;

    // Seleção dentro de um pai recolhido: abre o caminho até ela.
    if (ctx.selected != id && ctx.scene.IsDescendantOf(ctx.selected, id)) ImGui::SetNextItemOpen(true);

    const bool inactive = !ctx.scene.IsActiveInHierarchy(id);
    if (inactive) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(id)), flags, "%s",
                                        e->name.c_str());
    if (inactive) ImGui::PopStyleColor();

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) ctx.selected = id;

    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(kEntityPayload, &id, sizeof(id));
        ImGui::TextUnformatted(e->name.c_str());
        ImGui::EndDragDropSource();
    }
    AcceptEntityDrop(ctx, id);
    // Arquivos do painel Projeto: malha vira filho; textura vai para o Mesh Renderer desta entidade.
    for (const auto& file : AcceptPathDrop(motor::AssetType::Mesh)) ctx.CreateFromMeshFile(file, id);
    if (const auto files = AcceptPathDrop(motor::AssetType::Texture); !files.empty()) {
        ctx.ApplyTextureFile(id, files.front());
    }

    if (ImGui::BeginPopupContextItem()) {
        ctx.selected = id;
        if (ImGui::BeginMenu("Criar filho")) {
            DrawCreateEntityMenuItems(ctx, id);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Duplicar", "Ctrl+D")) ctx.DuplicateSelected();
        if (ImGui::MenuItem("Excluir", "Del")) ctx.pendingDelete = id;
        ImGui::EndPopup();
    }

    if (open) {
        for (EntityId child : children) DrawEntityNode(ctx, child);
        ImGui::TreePop();
    }
}
} // namespace

void DrawHierarchyPanel(EditorContext& ctx) {
    if (!ImGui::Begin("Hierarquia")) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("+ Criar")) ImGui::OpenPopup("CreateEntity");
    if (ImGui::BeginPopup("CreateEntity")) {
        DrawCreateEntityMenuItems(ctx, kNullEntity);
        ImGui::EndPopup();
    }
    ImGui::Separator();

    for (EntityId root : ctx.scene.Children(kNullEntity)) DrawEntityNode(ctx, root);

    // Área vazia restante: soltar aqui = mover para a raiz; clicar = limpar seleção.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##hierarchy_background", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 24.0f)));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) ctx.selected = kNullEntity;
    AcceptEntityDrop(ctx, kNullEntity);
    for (const auto& file : AcceptPathDrop(motor::AssetType::Mesh)) ctx.CreateFromMeshFile(file, kNullEntity);

    if (ImGui::BeginPopupContextItem("##hierarchy_background_ctx")) {
        DrawCreateEntityMenuItems(ctx, kNullEntity);
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace editor
