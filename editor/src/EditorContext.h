#pragma once

#include "EntityClipboard.h"

#include <motor/Motor.h>

#include <glm/gtc/quaternion.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace editor {

using motor::EntityId;
using motor::kNullEntity;

// Formas (Cubo a Cone) vêm com o Collider que bate com elas: corpo parado; com Rigidbody, cai.
enum class Primitive { Empty, Cube, Sphere, Plane, Camera, DirectionalLight, PointLight, SpotLight, Cylinder, Capsule, Cone };

// Arquivos arrastados do painel Projeto (payload do ImGui): caminhos UTF-8 um por linha.
inline constexpr const char* kPathPayload = "MOTOR_PATH";
// Entidade arrastada da Hierarquia (payload: um EntityId).
inline constexpr const char* kEntityPayload = "MOTOR_ENTITY";
void SetPathDragPayload(const std::vector<std::filesystem::path>& paths);
// Alvo de soltura no último item desenhado. Só reage (e só destaca o alvo) se houver algum arquivo do tipo
// pedido no que está sendo arrastado; devolve esses arquivos quando o usuário solta.
std::vector<std::filesystem::path> AcceptPathDrop(motor::AssetType type);
std::vector<std::filesystem::path> AcceptAnyPathDrop();  // qualquer arquivo ou pasta

// Estado compartilhado entre os painéis.
struct EditorContext {
    motor::Scene scene;
    motor::AssetLibrary assets;
    // Seleção: `selected` é a principal (Inspector, gizmo, foco) e `selection` todas, na ordem em que foram
    // escolhidas, com a principal no fim. Quem só troca `selected` (criar, MCP, desfazer) não precisa mexer
    // em `selection`: SyncSelection, chamado a cada frame, volta a seleção para só a principal.
    EntityId selected = kNullEntity;
    std::vector<EntityId> selection;
    EntityId pendingDelete = kNullEntity;  // processado no fim do frame, fora da iteração dos painéis

    bool IsSelected(EntityId id) const;
    size_t SelectionCount() const { return selection.size(); }
    void SelectOnly(EntityId id);  // kNullEntity limpa
    void AddToSelection(EntityId id);     // vira a principal
    void ToggleSelected(EntityId id);     // Ctrl+clique: entra (como principal) ou sai
    void SetSelection(const std::vector<EntityId>& ids);  // a última vira a principal
    // Selecionados sem nenhum ancestral também selecionado, na ordem da seleção: mover, duplicar e excluir
    // agem neles (os filhos vão junto com o pai).
    std::vector<EntityId> SelectionRoots() const;
    // Tira da seleção o que saiu da cena e a reduz à principal se esta foi trocada por fora.
    void SyncSelection();

    std::string pendingError;  // mostrado pelo EditorApp num popup

    // Material ou prefab editado no Inspector vira um passo de desfazer (UndoHistory::PushAction, ligado pelo
    // EditorApp). `playing`: no Play essas edições não são gravadas e voltam ao parar (RevertPlayAssetEdits).
    std::function<void(std::function<void()> undo, std::function<void()> redo)> pushAssetUndo;
    bool playing = false;

    // Prefabs do projeto (.mprefab) e o asset mostrado no Inspector quando nenhuma entidade está
    // selecionada (material ou prefab clicado no painel Projeto).
    motor::PrefabLibrary prefabs;
    std::filesystem::path inspectedAsset;

    // Pedido feito no Inspector/Hierarquia, aplicado no fim do frame (mexe nos componentes da entidade).
    enum class PrefabAction { None, Apply, Revert, Unpack };
    PrefabAction pendingPrefabAction = PrefabAction::None;
    EntityId pendingPrefabEntity = kNullEntity;

    // Cena trocada inteira (abrir, recuperar): sem seleção, sem exclusão pendente e com os ângulos do Inspector
    // recalculados.
    void ResetSceneState();
    // O Inspector guarda os ângulos de Euler que mostra (a conversão quat -> euler "salta"). Rotação mudada fora
    // dele: ForgetAngles(id) recalcula os de `id`; sem id (cena restaurada, scripts recarregados), os de todos.
    void ForgetAngles(EntityId id = kNullEntity);
    // Usado pelo Inspector a cada desenho do Transform: true se os ângulos de `id` precisam ser recalculados.
    // Consome os pedidos feitos até aqui.
    bool TakeForgottenAngles(EntityId id);

    motor::Entity& CreatePrimitive(Primitive type, EntityId parent = kNullEntity);
    // Nova entidade com a malha de um arquivo .obj do projeto. nullptr (e pendingError) se falhar.
    motor::Entity* CreateFromMeshFile(const std::filesystem::path& file, EntityId parent = kNullEntity);
    // Aplica uma textura do projeto ao Mesh Renderer da entidade.
    bool ApplyTextureFile(EntityId id, const std::filesystem::path& file);
    void DuplicateSelected();  // todos os selecionados; as cópias ficam selecionadas

    // Copiar / Recortar / Colar (Ctrl+C / Ctrl+X / Ctrl+V): os selecionados com os filhos. Colar vale em outra
    // cena; as coladas ficam selecionadas, na raiz ou sob `parent`, no mesmo lugar do mundo de onde saíram.
    EntityClipboard clipboard;
    uint64_t sceneTag = 1;  // muda quando a cena é trocada inteira (ResetSceneState)
    void CopySelected();
    void CutSelected();
    void Paste(EntityId parent = kNullEntity);
    // Exclui `pendingDelete`; se ele estiver selecionado, exclui toda a seleção.
    void ApplyPendingDelete();

    // Materiais (.mmat): aplicar um arquivo ao Mesh Renderer, ou gravar o material próprio dele como asset
    // novo em Assets/Materiais (o componente passa a usar o asset).
    bool ApplyMaterialFile(EntityId id, const std::filesystem::path& file);
    bool CreateMaterialFromRenderer(motor::MeshRenderer& renderer, const std::string& name);

    // Prefabs: nova instância do arquivo (selecionada); entidade -> prefab novo em `folder` (vazio = Assets/Prefabs).
    motor::Entity* InstantiatePrefabFile(const std::filesystem::path& file, EntityId parent = kNullEntity);
    bool CreatePrefabFromEntity(EntityId id, const std::filesystem::path& folder = {});
    // Modelos glTF (.gltf/.glb): importa se preciso (gera o prefab, motor/scene/ModelImport.h) e cria uma
    // instância do prefab; ReimportModelFile gera o prefab e os materiais de novo.
    motor::Entity* InstantiateModelFile(const std::filesystem::path& file, EntityId parent = kNullEntity);
    bool ReimportModelFile(const std::filesystem::path& file);
    void RequestPrefabAction(PrefabAction action, EntityId id);
    void ApplyPendingPrefabAction();
    // Fim do frame, fora do Play: as instâncias seguem os prefabs que mudaram e o que o usuário mudou numa
    // instância vira override. `forEachSnapshot` aplica o mesmo sync nas fotos do desfazer, senão desfazer
    // "desfaria" a mudança do prefab (e o histórico nunca ficaria igual à cena).
    void UpdatePrefabs(const std::function<void(const std::function<void(motor::Scene&)>&)>& forEachSnapshot);

    // Caminho livre para um asset novo em Assets/<folder> ("Nome", "Nome 2"...). Vazio sem projeto.
    std::filesystem::path NewAssetPath(const std::filesystem::path& folder, const std::string& name,
                                       const char* extension) const;

private:
    std::filesystem::path m_prefabRoot;  // projeto do cache de prefabs (trocou = limpa o cache)
    bool m_forgetAllAngles = false;
    std::vector<EntityId> m_forgottenAngles;
};

// Itens de menu "criar" reutilizados no menu principal e nos menus de contexto.
void DrawCreateEntityMenuItems(EditorContext& ctx, EntityId parent);

void DrawHierarchyPanel(EditorContext& ctx);
void FocusHierarchySearch();  // Ctrl+F: foco na busca da Hierarquia
void DrawInspectorPanel(EditorContext& ctx);
// Fim do Play: materiais e prefabs editados no Inspector durante o jogo voltam a ser como antes dele.
void RevertPlayAssetEdits(EditorContext& ctx);
void DrawScenePanel(EditorContext& ctx);

} // namespace editor
