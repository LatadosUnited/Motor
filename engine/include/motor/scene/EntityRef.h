#pragma once

// Referência a outra entidade da cena, como campo de componente ou de script: no Inspector é escolhida numa
// lista ou arrastada da Hierarquia, no lugar de buscar pelo nome no OnStart.
//
//   struct Seguir : motor::Script {
//       motor::EntityRef alvo;
//       void OnUpdate(float dt) override {
//           if (motor::Entity* e = GetEntity(alvo)) { ... }
//       }
//   };
//
// Guarda o id da entidade (o mesmo da cena salva e do Play). Duplicar entidades, criar um prefab e
// instanciá-lo ajustam as referências que apontam para dentro da parte copiada.

#include "motor/scene/Component.h"

#include <cstdint>
#include <functional>

namespace motor {

class Scene;
struct Entity;
using EntityId = uint32_t;

struct EntityRef {
    EntityId id = 0;  // 0 = nenhuma

    EntityRef() = default;
    EntityRef(EntityId entity) : id(entity) {}

    bool IsEmpty() const { return id == 0; }
    operator EntityId() const { return id; }  // Destroy(alvo), Send(alvo, ...), GetComponent<T>(alvo)
    bool operator==(const EntityRef& other) const { return id == other.id; }
};

template <> struct FieldTraits<EntityRef> {
    static CustomFieldType Describe();
};

inline constexpr const char* kEntityRefTypeName = "EntityRef";

// Troca as referências (campos EntityRef dos componentes de `entity`) pelo que `map` devolver; `map` devolve
// o próprio id para manter. Usado ao copiar entidades: as que apontam para dentro da cópia passam a apontar
// para a cópia correspondente.
void RemapEntityRefs(Entity& entity, const std::function<EntityId(EntityId)>& map);

} // namespace motor
