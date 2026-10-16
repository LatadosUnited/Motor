#include "EditorContext.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>

namespace editor {

namespace {
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

    // Instâncias de prefab em azul (mais claro na raiz), como no painel Projeto.
    const bool inactive = !ctx.scene.IsActiveInHierarchy(id);
    const bool prefabPart = e->Has<motor::PrefabInstance>();
    const bool prefabRoot = prefabPart && motor::Prefabs::IsInstanceRoot(ctx.scene, id, ctx.prefabs, ctx.assets);
    const bool colored = inactive || prefabPart;
    if (colored) {
        ImGui::PushStyleColor(ImGuiCol_Text, inactive     ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                                             : prefabRoot ? ImVec4(0.45f, 0.75f, 1.0f, 1.0f)
                                                          : ImVec4(0.6f, 0.78f, 0.95f, 1.0f));
    }
    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(id)), flags, "%s",
                                        e->name.c_str());
    if (colored) ImGui::PopStyleColor();

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) ctx.selected = id;

    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(kEntityPayload, &id, sizeof(id));
        ImGui::TextUnformatted(e->name.c_str());
        ImGui::EndDragDropSource();
    }
    AcceptEntityDrop(ctx, id);
    // Arquivos do painel Projeto: malha e prefab viram filhos; textura e material vão para o Mesh Renderer
    // desta entidade.
    for (const auto& file : AcceptPathDrop(motor::AssetType::Mesh)) ctx.CreateFromMeshFile(file, id);
    for (const auto& file : AcceptPathDrop(motor::AssetType::Prefab)) ctx.InstantiatePrefabFile(file, id);
    if (const auto files = AcceptPathDrop(motor::AssetType::Texture); !files.empty()) {
        ctx.ApplyTextureFile(id, files.front());
    }
    if (const auto files = AcceptPathDrop(motor::AssetType::Material); !files.empty()) {
        ctx.ApplyMaterialFile(id, files.front());
    }

    if (ImGui::BeginPopupContextItem()) {
        ctx.selected = id;
        if (ImGui::BeginMenu("Criar filho")) {
            DrawCreateEntityMenuItems(ctx, id);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Duplicar", "Ctrl+D")) ctx.DuplicateSelected();
        if (ImGui::MenuItem("Excluir", "Del")) ctx.pendingDelete = id;
        ImGui::Separator();
        if (ImGui::MenuItem("Criar prefab")) ctx.CreatePrefabFromEntity(id);
        ImGui::SetItemTooltip("Grava a entidade e os filhos em Assets/Prefabs e a transforma numa instância.\n"
                              "Também dá para arrastar a entidade para uma pasta do painel Projeto.");
        if (prefabPart) {
            using Action = EditorContext::PrefabAction;
            if (ImGui::MenuItem("Aplicar ao prefab")) ctx.RequestPrefabAction(Action::Apply, id);
            if (ImGui::MenuItem("Reverter para o prefab")) ctx.RequestPrefabAction(Action::Revert, id);
            if (ImGui::MenuItem("Desfazer vínculo com o prefab")) ctx.RequestPrefabAction(Action::Unpack, id);
        }
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
    for (const auto& file : AcceptPathDrop(motor::AssetType::Prefab)) ctx.InstantiatePrefabFile(file, kNullEntity);

    if (ImGui::BeginPopupContextItem("##hierarchy_background_ctx")) {
        DrawCreateEntityMenuItems(ctx, kNullEntity);
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace editor
