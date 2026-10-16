#pragma once

#include <motor/Motor.h>

#include <glm/gtc/quaternion.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace editor {

using motor::EntityId;
using motor::kNullEntity;

enum class Primitive { Empty, Cube, Sphere, Plane, Camera, DirectionalLight, PointLight, SpotLight };

// Arquivos arrastados do painel Projeto (payload do ImGui): caminhos UTF-8 um por linha.
inline constexpr const char* kPathPayload = "MOTOR_PATH";
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

    motor::Entity& CreatePrimitive(Primitive type, EntityId parent = kNullEntity);
    // Nova entidade com a malha de um arquivo .obj do projeto. nullptr (e pendingError) se falhar.
    motor::Entity* CreateFromMeshFile(const std::filesystem::path& file, EntityId parent = kNullEntity);
    // Aplica uma textura do projeto ao Mesh Renderer da entidade.
    bool ApplyTextureFile(EntityId id, const std::filesystem::path& file);
    void DuplicateSelected();
    void ApplyPendingDelete();
};

// Itens de menu "criar" reutilizados no menu principal e nos menus de contexto.
void DrawCreateEntityMenuItems(EditorContext& ctx, EntityId parent);

void DrawHierarchyPanel(EditorContext& ctx);
void DrawInspectorPanel(EditorContext& ctx);
void DrawScenePanel(EditorContext& ctx);

} // namespace editor
