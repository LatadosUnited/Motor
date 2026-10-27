#pragma once

#include "motor/renderer/Environment.h"
#include "motor/renderer/RenderWorld.h"
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
struct MeshRenderer;
struct Light;

using EntityId = uint32_t;
inline constexpr EntityId kNullEntity = 0;

// Contador global que sobe a cada entidade criada ou destruída, componente adicionado ou removido (Entity::Add e
// Remove) e cena copiada. Quem guarda algo derivado da estrutura das cenas (a lista de scripts do SceneRuntime, as
// listas por tipo de Scene::EntitiesWith) o compara para saber se precisa refazer, sem percorrer a cena.
uint64_t SceneStructureVersion();
// Para quem mexe direto em Entity::components (prefabs, serializador, editor) em vez de usar Add e Remove: além do
// contador, faz os índices de todas as cenas (tipos, filhos, nomes) serem refeitos na próxima consulta.
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
    Scene* owner = nullptr;                // cena que guarda a entidade: Add e Remove avisam os índices dela
};

struct Entity {
    EntityId id = kNullEntity;
    std::string name;
    bool active = true;
    EntityId parent = kNullEntity;  // troque com Scene::SetParent (o índice de filhos da cena não vê a escrita direta)
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

    // As entidades ficam em blocos contínuos (BlockPool, Fase 28), na ordem de criação, como os componentes.
    static void* operator new(size_t size);
    static void operator delete(void* p, size_t size);

private:
    template <class T>
    static const ComponentType* TypeOf() {
        const ComponentType* type = ComponentRegistry::TypeOf<T>();
        if (!type) ThrowUnregisteredComponent();
        return type;
    }
};

// Mesmo id, nome, ativo, pai, transform e componentes (com os mesmos valores, na mesma ordem): o que Scene::operator==
// compara em cada entidade.
bool SameEntity(const Entity& a, const Entity& b);

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
    Scene(Scene&& other) noexcept;  // as entidades passam a apontar para a cena nova (EntityCache::owner)
    Scene& operator=(Scene&& other) noexcept;

    // Mesmo conteúdo: entidades (na mesma ordem, com os mesmos ids e valores) e ambiente.
    bool operator==(const Scene& other) const;

    // Desfazer do editor (Fase 27), que guarda as entidades e não a cena inteira: uma cena com cópias de `entities`
    // (em ordem de id, como Entities(), com os mesmos ids) e o próximo id que ela vai dar.
    static Scene FromEntities(const std::vector<const Entity*>& entities, EntityId nextId,
                              const Environment& environment);
    EntityId NextEntityId() const { return m_nextId; }

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
    // Uma entidade com esse nome, pelo índice de nomes: a primeira na ordem da cena, salvo quando há nomes repetidos e
    // uma entidade foi renomeada direto (o nome é campo livre, sem aviso) para o nome de outra que vem depois; aí pode
    // vir a de depois, até o índice ser refeito (como o GameObject.Find da Unity, que não garante qual). Quem foi
    // renomeada é achada procurando na cena (e entra no índice). Nome que não existe percorre a cena.
    Entity* FindByName(std::string_view name);

    // Câmera do jogo: a primeira entidade ativa com Câmera marcada como principal (senão, a primeira ativa
    // com Câmera). kNullEntity se não houver.
    EntityId FindMainCamera() const;
    // Posiciona `camera` no lugar da entidade e copia fov/near/far do componente (aspect fica como está).
    bool ApplyCamera(EntityId id, Camera& camera) const;

    const std::vector<std::unique_ptr<Entity>>& Entities() const { return m_entities; }
    // Ids das entidades com o componente, na ordem da cena (ativas ou não), sem percorrer a cena. Os índices (tipos,
    // filhos, nomes) são montados numa passada na primeira consulta e depois só acompanham as mudanças (criar,
    // destruir, Add, Remove, SetParent), aplicadas na próxima consulta; MarkSceneStructureChanged os refaz. A lista
    // muda na próxima chamada (desta ou de Children/FindByName) depois de criar ou destruir entidades e de adicionar
    // ou remover componentes: quem faz isso dentro do laço (ou chama código do jogo) percorre uma cópia.
    const std::vector<EntityId>& EntitiesWith(const ComponentType* type) const;
    template <class T> const std::vector<EntityId>& EntitiesWith() const {
        return EntitiesWith(ComponentRegistry::TypeOf<T>());
    }
    std::vector<EntityId> Children(EntityId parent) const;  // kNullEntity => raízes; na ordem da cena, pelo índice

    bool IsDescendantOf(EntityId id, EntityId ancestor) const;
    bool IsActiveInHierarchy(EntityId id) const;
    bool IsActiveInHierarchy(const Entity& e) const;  // sem a busca pelo id

    // Falha (retorna false) se criar um ciclo. keepWorldTransform preserva a posição no mundo.
    bool SetParent(EntityId id, EntityId newParent, bool keepWorldTransform = true);

    // Com cache por entidade: recalcula só quando o transform, o pai ou o mundo do pai mudaram.
    glm::mat4 WorldMatrix(EntityId id) const;
    // O mesmo sem a busca pelo id (a entidade precisa ser desta cena); a referência vale até a próxima consulta.
    const glm::mat4& WorldMatrix(const Entity& e) const { return CachedWorld(e); }
    // Ajusta o Transform local para que a entidade fique com esta matriz no mundo.
    void SetWorldMatrix(EntityId id, const glm::mat4& world);

    // Luzes das entidades ativas com o componente Luz, na ordem da cena, já no espaço do mundo.
    std::vector<Light> CollectLights() const;

    // Render = BeginScene + SubmitTo + EndScene. SubmitTo serve para quem quer desenhar algo a mais na mesma passada.
    void Render(Renderer& renderer, const Camera& camera, const Framebuffer* target = nullptr) const;
    void SubmitTo(Renderer& renderer) const;
    // Os Mesh Renderer estáticos (Fase 28) vão para o renderer guardados (RenderWorld), refeitos só quando a estrutura
    // da cena muda ou depois desta chamada: quem muda um estático (posição, malha, material) avisa aqui. O editor
    // avisa a cada edição; no jogo, os scripts.
    void MarkRenderDirty() { m_static.dirty = true; }
    // Quantos objetos estáticos estão guardados para o renderer (depois do último SubmitTo).
    size_t StaticRenderCount() const { return m_static.world.Count(); }

    // Raio contra os triângulos das malhas visíveis (cópia na CPU da Mesh; malhas com pele, na pose de repouso).
    // direction não precisa ser normalizada; `distance` é medida em unidades de direction.
    std::optional<RaycastHit> Raycast(const glm::vec3& origin, const glm::vec3& direction) const;

    // Céu, neblina e luz ambiente. As luzes com direção são entidades com o componente Luz.
    Environment environment;

private:
    friend struct Entity;  // Add e Remove avisam o índice

    const Entity* ParentOf(const Entity& e) const;  // Find(e.parent) sem a busca, se a estrutura não mudou
    const glm::mat4& CachedWorld(const Entity& e) const;
    // m_meshList e os estáticos (m_static) em dia com a cena; false se não há Mesh Renderer.
    bool SyncRenderLists() const;

    // Mudança guardada para o índice, aplicada na próxima consulta (UpdateIndex). As listas ficam ordenadas por id,
    // que é a ordem da cena: as entidades só entram no fim e os ids só crescem (a cópia preserva os dois).
    struct IndexChange {
        enum Kind : uint8_t { AddType, RemoveType, AddChild, RemoveChild, AddName, RemoveName } kind;
        EntityId id;
        const ComponentType* type = nullptr;  // AddType, RemoveType
        EntityId parent = kNullEntity;        // AddChild, RemoveChild
        size_t nameHash = 0;                  // AddName, RemoveName
    };
    bool Indexed() const;  // o índice está montado e acompanha as mudanças (senão, a próxima consulta o monta)
    void Record(const IndexChange& change) { if (Indexed()) m_pending.push_back(change); }
    void RecordEntity(const Entity& e, bool added);  // filho, nome e tipos de uma entidade que entra ou sai
    void UpdateIndex() const;

    // `copies`: pares (original, cópia) de cada entidade copiada, para ajustar as EntityRef.
    EntityId CloneRecursive(const Entity& source, EntityId newParent,
                            std::vector<std::pair<EntityId, EntityId>>& copies);

    std::vector<std::unique_ptr<Entity>> m_entities;
    std::unordered_map<EntityId, Entity*> m_byId;
    EntityId m_nextId = 1;

    // Índices de EntitiesWith, Children e FindByName (por hash do nome, conferido na busca). Montados enquanto
    // m_indexVersion é a versão global dos índices (MarkSceneStructureChanged a sobe).
    mutable std::unordered_map<const ComponentType*, std::vector<EntityId>> m_byType;
    mutable std::unordered_map<EntityId, std::vector<EntityId>> m_children;
    mutable std::unordered_map<size_t, std::vector<EntityId>> m_byName;
    mutable std::vector<IndexChange> m_pending;
    mutable uint64_t m_indexVersion = UINT64_MAX;

    // SubmitTo (Fase 26): entidade e MeshRenderer de cada id de EntitiesWith<MeshRenderer>, lado a lado, refeita só
    // quando a estrutura muda (SceneStructureVersion). Sem ela, cada objeto passava pelo mapa de ids e pela lista de
    // componentes da entidade (~5 leituras espalhadas na memória). Cópia e movimento começam vazios.
    struct MeshList {
        std::vector<std::pair<const Entity*, const MeshRenderer*>> items;
        uint64_t version = UINT64_MAX;
        MeshList() = default;
        MeshList(const MeshList&) {}
        MeshList& operator=(const MeshList&) {
            items.clear();
            version = UINT64_MAX;
            return *this;
        }
    };
    mutable MeshList m_meshList;

    // SubmitTo (Fase 28): os estáticos guardados para o renderer. Aponta para os componentes desta cena: cópia e
    // movimento começam vazios (e sujos).
    struct StaticRender {
        RenderWorld world;
        std::vector<EntityId> entities;  // id no world -> entidade (Raycast)
        std::vector<float> enter;        // Raycast: entrada do raio na caixa de cada acerto da grade
        std::vector<uint32_t> dynamic;  // posições em m_meshList.items dos que não são estáticos
        uint64_t version = UINT64_MAX;
        bool dirty = true;
        StaticRender() = default;
        StaticRender(const StaticRender&) {}
        StaticRender& operator=(const StaticRender&) {
            world.Clear();
            entities.clear();
            dynamic.clear();
            version = UINT64_MAX;
            dirty = true;
            return *this;
        }
    };
    mutable StaticRender m_static;
};

} // namespace motor
