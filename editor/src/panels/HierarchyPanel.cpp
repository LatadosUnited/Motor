#include "EditorContext.h"
#include "panels/HierarchyFilter.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>

namespace editor {

namespace {
// Ordem das linhas (o Shift+clique seleciona o intervalo entre a âncora e a linha clicada), incluindo as que não
// cabem na janela. A do frame anterior é a que vale: a deste ainda está sendo montada quando o clique chega.
std::vector<EntityId> g_rows;
std::vector<EntityId> g_lastRows;
EntityId g_anchor = kNullEntity;  // último clique sem Shift
// Lista virtual (Fase 27): a árvore vira uma lista das linhas visíveis (pais abertos) e só as que cabem na janela
// são desenhadas. Com milhares de entidades, desenhar todas custava milissegundos por quadro.
struct Row {
    EntityId id = kNullEntity;
    int depth = 0;
    bool hasChildren = false;
};
std::vector<Row> g_visible;
std::unordered_set<EntityId> g_collapsed;  // pais fechados (começam abertos)
EntityId g_revealed = kNullEntity;         // última seleção cujo caminho foi aberto
// Clique simples num item já selecionado entre vários: só vira seleção única ao soltar sem ter arrastado
// (senão arrastar o grupo para outro pai seria impossível).
EntityId g_collapseOnRelease = kNullEntity;
std::string g_search;        // texto da busca (HierarchyFilter)
bool g_focusSearch = false;  // Ctrl+F

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

// Linhas da árvore na ordem da Hierarquia, sem os filhos dos pais fechados.
void CollectRows(const motor::Scene& scene, EntityId parent, int depth) {
    for (EntityId id : scene.Children(parent)) {
        const std::vector<EntityId> children = scene.Children(id);
        g_visible.push_back({id, depth, !children.empty()});
        if (!children.empty() && !g_collapsed.contains(id)) CollectRows(scene, id, depth + 1);
    }
}

// Seleção nova dentro de um pai fechado (clique no viewport, busca, MCP): abre o caminho até ela, uma vez (depois
// o usuário pode fechar o pai de novo).
void RevealSelection(const EditorContext& ctx) {
    if (ctx.selected == g_revealed) return;
    g_revealed = ctx.selected;
    const motor::Entity* e = ctx.scene.Find(ctx.selected);
    for (int guard = 0; e && e->parent != kNullEntity && guard < 10000; ++guard) {
        g_collapsed.erase(e->parent);
        e = ctx.scene.Find(e->parent);
    }
}

// Uma linha da Hierarquia (os filhos são outras linhas, logo abaixo, com `depth` maior); `flat` (resultado da
// busca): sem seta de abrir, com o caminho do pai ao lado.
void DrawEntityRow(EditorContext& ctx, const Row& row, bool flat) {
    const EntityId id = row.id;
    motor::Entity* e = ctx.scene.Find(id);
    if (!e) return;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (flat || !row.hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
    if (ctx.IsSelected(id)) flags |= ImGuiTreeNodeFlags_Selected;
    const bool collapsed = g_collapsed.contains(id);
    if (!flat && row.hasChildren) ImGui::SetNextItemOpen(!collapsed, ImGuiCond_Always);
    const float indent = static_cast<float>(row.depth) * ImGui::GetStyle().IndentSpacing;
    if (indent > 0.0f) ImGui::Indent(indent);

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
    if (indent > 0.0f) ImGui::Unindent(indent);
    // Clique na seta: vale a partir do próximo quadro (as linhas deste já foram montadas).
    if (!flat && row.hasChildren && open == collapsed) {
        if (open) {
            g_collapsed.erase(id);
        } else {
            g_collapsed.insert(id);
        }
    }
    if (flat) {
        const std::string path = ParentPath(ctx.scene, id);
        if (!path.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", path.c_str());
        }
        ImGui::SetItemTooltip("Em: %s", path.empty() ? "(raiz)" : path.c_str());
    }

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
}

// As linhas que aparecem na janela (o ImGuiListClipper pula as outras, só reservando a altura delas).
void DrawRows(EditorContext& ctx, bool flat) {
    g_rows.clear();
    for (const Row& row : g_visible) g_rows.push_back(row.id);  // ordem completa, para o Shift+clique
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(g_visible.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            DrawEntityRow(ctx, g_visible[static_cast<size_t>(i)], flat);
        }
    }
    g_lastRows.swap(g_rows);
}

// Caixa de busca. Devolve o filtro do texto atual (vazio = árvore normal).
HierarchyFilter DrawSearchBox() {
    if (std::exchange(g_focusSearch, false)) ImGui::SetKeyboardFocusHere();
    const bool searching = !g_search.empty();
    ImGui::SetNextItemWidth(searching ? -ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x : -FLT_MIN);
    ImGui::InputTextWithHint("##hierarchy_search", "Buscar (Ctrl+F)", &g_search,
                             ImGuiInputTextFlags_EscapeClearsAll);
    ImGui::SetItemTooltip("Procura no nome; t: procura pelo componente (t:luz, t:collider, t:Girar).\n"
                          "Várias palavras: todas precisam bater. Esc limpa.");
    if (searching) {
        ImGui::SameLine();
        if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0.0f))) g_search.clear();
        ImGui::SetItemTooltip("Limpar a busca");
    }
    return HierarchyFilter::Parse(g_search);
}
} // namespace

void FocusHierarchySearch() {
    g_focusSearch = true;
    ImGui::SetWindowFocus("Hierarquia");
}

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
    ImGui::SameLine();
    const HierarchyFilter filter = DrawSearchBox();
    ImGui::Separator();

    g_visible.clear();
    if (filter.Empty()) {
        RevealSelection(ctx);
        CollectRows(ctx.scene, kNullEntity, 0);
        DrawRows(ctx, false);
    } else {
        // Resultados numa lista só, na ordem da Hierarquia, com o caminho de cada um.
        const std::vector<EntityId> found = SearchHierarchy(ctx.scene, filter);
        ImGui::TextDisabled("%zu encontrada(s)", found.size());
        if (!found.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Selecionar todas")) ctx.SetSelection(found);
        }
        for (EntityId id : found) g_visible.push_back({id, 0, false});
        DrawRows(ctx, true);
    }

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
