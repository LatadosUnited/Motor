#include "EditorContext.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>

namespace editor {

namespace {
// Ordem das linhas desenhadas (o Shift+clique seleciona o intervalo entre a âncora e a linha clicada). A do
// frame anterior é a completa: a deste ainda está sendo montada quando o clique chega.
std::vector<EntityId> g_rows;
std::vector<EntityId> g_lastRows;
EntityId g_anchor = kNullEntity;  // último clique sem Shift
// Clique simples num item já selecionado entre vários: só vira seleção única ao soltar sem ter arrastado
// (senão arrastar o grupo para outro pai seria impossível).
EntityId g_collapseOnRelease = kNullEntity;

void AcceptEntityDrop(EditorContext& ctx, EntityId newParent) {
    if (!ImGui::BeginDragDropTarget()) return;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload)) {
        const EntityId dragged = *static_cast<const EntityId*>(payload->Data);
        // Arrastou um dos selecionados: vão todos (os de cima; os filhos acompanham o pai).
        if (ctx.IsSelected(dragged)) {
            for (EntityId id : ctx.SelectionRoots()) ctx.scene.SetParent(id, newParent);  // recusa ciclos sozinho
        } else {
            ctx.scene.SetParent(dragged, newParent);
        }
    }
    ImGui::EndDragDropTarget();
}

void SelectRange(EditorContext& ctx, EntityId from, EntityId to) {
    const auto a = std::find(g_lastRows.begin(), g_lastRows.end(), from);
    const auto b = std::find(g_lastRows.begin(), g_lastRows.end(), to);
    if (a == g_lastRows.end() || b == g_lastRows.end()) {
        ctx.AddToSelection(to);
        return;
    }
    const auto [first, last] = std::minmax(a, b);
    for (auto it = first; it != last + 1; ++it) {
        if (*it != to) ctx.AddToSelection(*it);
    }
    ctx.AddToSelection(to);  // a clicada vira a principal
}

void OnRowClicked(EditorContext& ctx, EntityId id) {
    const ImGuiIO& io = ImGui::GetIO();
    g_collapseOnRelease = kNullEntity;
    // Âncora: o último clique sem Shift; se ela saiu da seleção (clique no viewport, Esc), a principal.
    const EntityId anchor = ctx.IsSelected(g_anchor) ? g_anchor : ctx.selected;
    if (io.KeyShift && anchor != kNullEntity) {
        if (!io.KeyCtrl) ctx.SelectOnly(kNullEntity);  // Shift sozinho troca a seleção pelo intervalo
        SelectRange(ctx, anchor, id);
        g_anchor = anchor;
        return;
    }
    g_anchor = id;
    if (io.KeyCtrl) {
        ctx.ToggleSelected(id);
    } else if (ctx.IsSelected(id) && ctx.SelectionCount() > 1) {
        ctx.AddToSelection(id);  // vira a principal; os outros continuam até soltar
        g_collapseOnRelease = id;
    } else {
        ctx.SelectOnly(id);
    }
}

void DrawEntityNode(EditorContext& ctx, EntityId id) {
    motor::Entity* e = ctx.scene.Find(id);
    if (!e) return;

    const std::vector<EntityId> children = ctx.scene.Children(id);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (ctx.IsSelected(id)) flags |= ImGuiTreeNodeFlags_Selected;
    g_rows.push_back(id);

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

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) OnRowClicked(ctx, id);
    if (g_collapseOnRelease == id && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (ImGui::IsItemHovered()) ctx.SelectOnly(id);
        g_collapseOnRelease = kNullEntity;
    }

    if (ImGui::BeginDragDropSource()) {
        g_collapseOnRelease = kNullEntity;  // arrastou: a seleção fica
        ImGui::SetDragDropPayload(kEntityPayload, &id, sizeof(id));
        if (ctx.IsSelected(id) && ctx.SelectionCount() > 1) {
            ImGui::Text("%s e mais %zu", e->name.c_str(), ctx.SelectionCount() - 1);
        } else {
            ImGui::TextUnformatted(e->name.c_str());
        }
        ImGui::EndDragDropSource();
    }
    AcceptEntityDrop(ctx, id);
    // Arquivos do painel Projeto: malha e prefab viram filhos; textura e material vão para o Mesh Renderer
    // desta entidade.
    for (const auto& file : AcceptPathDrop(motor::AssetType::Mesh)) ctx.CreateFromMeshFile(file, id);
    for (const auto& file : AcceptPathDrop(motor::AssetType::Model)) ctx.InstantiateModelFile(file, id);
    for (const auto& file : AcceptPathDrop(motor::AssetType::Prefab)) ctx.InstantiatePrefabFile(file, id);
    if (const auto files = AcceptPathDrop(motor::AssetType::Texture); !files.empty()) {
        ctx.ApplyTextureFile(id, files.front());
    }
    if (const auto files = AcceptPathDrop(motor::AssetType::Material); !files.empty()) {
        ctx.ApplyMaterialFile(id, files.front());
    }

    if (ImGui::BeginPopupContextItem()) {
        if (!ctx.IsSelected(id)) ctx.SelectOnly(id);  // clique direito num dos selecionados mantém o grupo
        if (ImGui::BeginMenu("Criar filho")) {
            DrawCreateEntityMenuItems(ctx, id);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Recortar", "Ctrl+X")) ctx.CutSelected();  // todos os selecionados
        if (ImGui::MenuItem("Copiar", "Ctrl+C")) ctx.CopySelected();
        if (ImGui::MenuItem("Colar", "Ctrl+V", false, !ctx.clipboard.Empty())) ctx.Paste();
        if (ImGui::MenuItem("Colar como filho", nullptr, false, !ctx.clipboard.Empty())) ctx.Paste(id);
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

    g_rows.clear();
    for (EntityId root : ctx.scene.Children(kNullEntity)) DrawEntityNode(ctx, root);
    g_lastRows.swap(g_rows);

    // Área vazia restante: soltar aqui = mover para a raiz; clicar = limpar seleção.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##hierarchy_background", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 24.0f)));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyCtrl) ctx.SelectOnly(kNullEntity);
    AcceptEntityDrop(ctx, kNullEntity);
    for (const auto& file : AcceptPathDrop(motor::AssetType::Mesh)) ctx.CreateFromMeshFile(file, kNullEntity);
    for (const auto& file : AcceptPathDrop(motor::AssetType::Model)) ctx.InstantiateModelFile(file, kNullEntity);
    for (const auto& file : AcceptPathDrop(motor::AssetType::Prefab)) ctx.InstantiatePrefabFile(file, kNullEntity);

    if (ImGui::BeginPopupContextItem("##hierarchy_background_ctx")) {
        DrawCreateEntityMenuItems(ctx, kNullEntity);
        ImGui::Separator();
        if (ImGui::MenuItem("Colar", "Ctrl+V", false, !ctx.clipboard.Empty())) ctx.Paste();
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace editor
