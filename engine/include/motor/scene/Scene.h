#pragma once

#include "motor/renderer/Material.h"
#include "motor/renderer/Mesh.h"
#include "motor/renderer/Renderer.h"
#include "motor/scene/Transform.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace motor {

class Camera;
class Framebuffer;

using EntityId = uint32_t;
inline constexpr EntityId kNullEntity = 0;

struct MeshRenderer {
    std::shared_ptr<Mesh> mesh;
    Material material;
};

struct Entity {
    EntityId id = kNullEntity;
    std::string name;
    bool active = true;
    EntityId parent = kNullEntity;
    Transform transform;  // relativo ao pai
    std::optional<MeshRenderer> meshRenderer;
};

struct RaycastHit {
    EntityId entity = kNullEntity;
    float distance = 0.0f;
};

// Cena com hierarquia simples de entidades. Os filhos aparecem na ordem de criação.
class Scene {
public:
    Scene() = default;
    // Cópia profunda (entidades duplicadas, ids preservados; malhas/texturas compartilhadas). Usada pelo
    // desfazer do editor e para guardar o estado da cena antes do modo Play.
    Scene(const Scene& other);
    Scene& operator=(const Scene& other);
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;

    // Mesmo conteúdo: entidades (na mesma ordem, com os mesmos ids e valores), luz e ambiente.
    bool operator==(const Scene& other) const;

    Entity& CreateEntity(std::string name, EntityId parent = kNullEntity);
    void DestroyEntity(EntityId id);               // destrói também os descendentes
    EntityId Duplicate(EntityId id);               // copia a sub-árvore, mesmo pai

    Entity* Find(EntityId id);
    const Entity* Find(EntityId id) const;

    const std::vector<std::unique_ptr<Entity>>& Entities() const { return m_entities; }
    std::vector<EntityId> Children(EntityId parent) const;  // kNullEntity => raízes

    bool IsDescendantOf(EntityId id, EntityId ancestor) const;
    bool IsActiveInHierarchy(EntityId id) const;

    // Falha (retorna false) se criar um ciclo. keepWorldTransform preserva a posição no mundo.
    bool SetParent(EntityId id, EntityId newParent, bool keepWorldTransform = true);

    glm::mat4 WorldMatrix(EntityId id) const;
    // Ajusta o Transform local para que a entidade fique com esta matriz no mundo.
    void SetWorldMatrix(EntityId id, const glm::mat4& world);

    // Render = BeginScene + SubmitTo + EndScene. SubmitTo serve para quem quer desenhar algo a mais na mesma passada.
    void Render(Renderer& renderer, const Camera& camera, const Framebuffer* target = nullptr) const;
    void SubmitTo(Renderer& renderer) const;

    // Raio contra as caixas (AABB locais) das malhas visíveis. direction não precisa ser normalizada.
    std::optional<RaycastHit> Raycast(const glm::vec3& origin, const glm::vec3& direction) const;

    DirectionalLight light;
    Environment environment;

private:
    EntityId CloneRecursive(const Entity& source, EntityId newParent);

    std::vector<std::unique_ptr<Entity>> m_entities;
    std::unordered_map<EntityId, Entity*> m_byId;
    EntityId m_nextId = 1;
};

} // namespace motor
