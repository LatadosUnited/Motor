#pragma once

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

enum class Primitive { Empty, Cube, Sphere, Plane, Camera, DirectionalLight, PointLight, SpotLight };

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
    EntityId selected = kNullEntity;
    EntityId pendingDelete = kNullEntity;  // processado no fim do frame, fora da iteração dos painéis

    // Cache dos ângulos de Euler mostrados no Inspector, para evitar "saltos" na conversão quat <-> euler.
    EntityId eulerEntity = kNullEntity;
    glm::quat eulerSource{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 eulerDegrees{0.0f};

    std::string pendingError;  // mostrado pelo EditorApp num popup

    // Prefabs do projeto (.mprefab) e o asset mostrado no Inspector quando nenhuma entidade está
    // selecionada (material ou prefab clicado no painel Projeto).
    motor::PrefabLibrary prefabs;
    std::filesystem::path inspectedAsset;

    // Pedido feito no Inspector/Hierarquia, aplicado no fim do frame (mexe nos componentes da entidade).
    enum class PrefabAction { None, Apply, Revert, Unpack };
    PrefabAction pendingPrefabAction = PrefabAction::None;
    EntityId pendingPrefabEntity = kNullEntity;

    motor::Entity& CreatePrimitive(Primitive type, EntityId parent = kNullEntity);
    // Nova entidade com a malha de um arquivo .obj do projeto. nullptr (e pendingError) se falhar.
    motor::Entity* CreateFromMeshFile(const std::filesystem::path& file, EntityId parent = kNullEntity);
    // Aplica uma textura do projeto ao Mesh Renderer da entidade.
    bool ApplyTextureFile(EntityId id, const std::filesystem::path& file);
    void DuplicateSelected();
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
};

// Itens de menu "criar" reutilizados no menu principal e nos menus de contexto.
void DrawCreateEntityMenuItems(EditorContext& ctx, EntityId parent);

void DrawHierarchyPanel(EditorContext& ctx);
void DrawInspectorPanel(EditorContext& ctx);
void DrawScenePanel(EditorContext& ctx);

} // namespace editor
