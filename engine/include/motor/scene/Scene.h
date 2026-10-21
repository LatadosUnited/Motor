#pragma once

#include "motor/renderer/Environment.h"
#include "motor/scene/Component.h"
#include "motor/scene/Components.h"
#include "motor/scene/Transform.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace motor {

class Camera;
class Framebuffer;
class Mesh;
class Renderer;
struct Light;

using EntityId = uint32_t;
inline constexpr EntityId kNullEntity = 0;

// Contador global que sobe a cada entidade criada ou destruída, componente adicionado ou removido (Entity::Add e
// Remove) e cena copiada. Quem guarda algo derivado da estrutura das cenas (a lista de scripts do SceneRuntime, as
// listas por tipo de Scene::EntitiesWith) o compara para saber se precisa refazer, sem percorrer a cena.
uint64_t SceneStructureVersion();
// Para quem mexe direto em Entity::components (prefabs, serializador, editor) em vez de usar Add e Remove.
void MarkSceneStructureChanged();

struct Entity;

// Cache da Scene por entidade (matriz de mundo e ponteiro do pai), conferido a cada consulta: não precisa de aviso
// quando o transform muda (scripts e o editor mexem direto em Entity::transform). A cópia de uma entidade começa
// sem cache.
struct EntityCache {
    EntityCache() = default;
    EntityCache(const EntityCache&) {}
    EntityCache& operator=(const EntityCache&) { valid = false; return *this; }

    bool valid = false;
    Transform local;                       // transform de quando `world` foi calculado
    EntityId parent = kNullEntity;         // pai de parentPtr
    const Entity* parentPtr = nullptr;     // vale enquanto SceneStructureVersion() == structure
    uint64_t structure = 0;
    uint64_t parentStamp = 0;              // `stamp` do pai quando foi calculado
    uint64_t stamp = 0;                    // sobe (contador global) a cada vez que `world` é recalculado
    glm::mat4 world{1.0f};
};

struct Entity {
    EntityId id = kNullEntity;
    std::string name;
    bool active = true;
    EntityId parent = kNullEntity;
    Transform transform;  // relativo ao pai; toda entidade tem

    // No máximo um componente de cada tipo, na ordem em que foram adicionados (é a ordem do Inspector).
    std::vector<ComponentPtr> components;

    template <class T> T* Get() { return static_cast<T*>(Get(*TypeOf<T>())); }
    template <class T> const T* Get() const { return static_cast<const T*>(Get(*TypeOf<T>())); }
    template <class T> bool Has() const { return Get<T>() != nullptr; }
    template <class T> T& Add() { return *static_cast<T*>(Add(*TypeOf<T>())); }
    template <class T> void Remove() { Remove(*TypeOf<T>()); }

    void* Get(const ComponentType& type);
    const void* Get(const ComponentType& type) const;
    void* Add(const ComponentType& type);  // se já existir, devolve o existente
    void Remove(const ComponentType& type);

    mutable EntityCache cache;  // interno da Scene (WorldMatrix, IsActiveInHierarchy)

private:
    template <class T>
    static const ComponentType* TypeOf() {
        const ComponentType* type = ComponentRegistry::TypeOf<T>();
        if (!type) ThrowUnregisteredComponent();
        return type;
    }
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

    // Mesmo conteúdo: entidades (na mesma ordem, com os mesmos ids e valores) e ambiente.
    bool operator==(const Scene& other) const;

    Entity& CreateEntity(std::string name, EntityId parent = kNullEntity);
    void DestroyEntity(EntityId id);               // destrói também os descendentes
    // Várias sub-árvores de uma vez, numa passada pela cena (DestroyEntity em laço percorre a cena a cada uma).
    // Ids repetidos, inexistentes ou dentro de outra sub-árvore da lista são ignorados.
    void DestroyEntities(const std::vector<EntityId>& roots);
    // As entidades que DestroyEntities(roots) destruiria: cada raiz seguida dos seus descendentes na ordem da
    // cena, as raízes na ordem dada; raiz que está dentro de outra sub-árvore da lista aparece só nela.
    std::vector<EntityId> Subtrees(const std::vector<EntityId>& roots) const;
    EntityId Duplicate(EntityId id);               // copia a sub-árvore, mesmo pai

    Entity* Find(EntityId id);
    const Entity* Find(EntityId id) const;
    Entity* FindByName(std::string_view name);  // a primeira com esse nome, na ordem da cena

    // Câmera do jogo: a primeira entidade ativa com Câmera marcada como principal (senão, a primeira ativa
    // com Câmera). kNullEntity se não houver.
    EntityId FindMainCamera() const;
    // Posiciona `camera` no lugar da entidade e copia fov/near/far do componente (aspect fica como está).
    bool ApplyCamera(EntityId id, Camera& camera) const;

    const std::vector<std::unique_ptr<Entity>>& Entities() const { return m_entities; }
    // Ids das entidades com o componente, na ordem da cena (ativas ou não), sem percorrer a cena: as listas de todos
    // os tipos são montadas numa passada só e refeitas quando a estrutura muda (SceneStructureVersion). A lista
    // muda na próxima chamada depois de criar ou destruir entidades e de adicionar ou remover componentes: quem faz
    // isso dentro do laço (ou chama código do jogo) percorre uma cópia.
    const std::vector<EntityId>& EntitiesWith(const ComponentType* type) const;
    template <class T> const std::vector<EntityId>& EntitiesWith() const {
        return EntitiesWith(ComponentRegistry::TypeOf<T>());
    }
    std::vector<EntityId> Children(EntityId parent) const;  // kNullEntity => raízes

    bool IsDescendantOf(EntityId id, EntityId ancestor) const;
    bool IsActiveInHierarchy(EntityId id) const;

    // Falha (retorna false) se criar um ciclo. keepWorldTransform preserva a posição no mundo.
    bool SetParent(EntityId id, EntityId newParent, bool keepWorldTransform = true);

    // Com cache por entidade: recalcula só quando o transform, o pai ou o mundo do pai mudaram.
    glm::mat4 WorldMatrix(EntityId id) const;
    // Ajusta o Transform local para que a entidade fique com esta matriz no mundo.
    void SetWorldMatrix(EntityId id, const glm::mat4& world);

    // Luzes das entidades ativas com o componente Luz, na ordem da cena, já no espaço do mundo.
    std::vector<Light> CollectLights() const;

    // Render = BeginScene + SubmitTo + EndScene. SubmitTo serve para quem quer desenhar algo a mais na mesma passada.
    void Render(Renderer& renderer, const Camera& camera, const Framebuffer* target = nullptr) const;
    void SubmitTo(Renderer& renderer) const;

    // Raio contra os triângulos das malhas visíveis (cópia na CPU da Mesh; malhas com pele, na pose de repouso).
    // direction não precisa ser normalizada; `distance` é medida em unidades de direction.
    std::optional<RaycastHit> Raycast(const glm::vec3& origin, const glm::vec3& direction) const;

    // Céu, neblina e luz ambiente. As luzes com direção são entidades com o componente Luz.
    Environment environment;

private:
    const Entity* ParentOf(const Entity& e) const;  // Find(e.parent) sem a busca, se a estrutura não mudou
    bool IsActiveInHierarchy(const Entity& e) const;
    const glm::mat4& CachedWorld(const Entity& e) const;

    // `copies`: pares (original, cópia) de cada entidade copiada, para ajustar as EntityRef.
    EntityId CloneRecursive(const Entity& source, EntityId newParent,
                            std::vector<std::pair<EntityId, EntityId>>& copies);

    std::vector<std::unique_ptr<Entity>> m_entities;
    std::unordered_map<EntityId, Entity*> m_byId;
    EntityId m_nextId = 1;

    // EntitiesWith: listas por tipo, válidas enquanto SceneStructureVersion() == m_byTypeVersion.
    mutable std::unordered_map<const ComponentType*, std::vector<EntityId>> m_byType;
    mutable uint64_t m_byTypeVersion = UINT64_MAX;
};

} // namespace motor
