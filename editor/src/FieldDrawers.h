#pragma once

#include "EditorContext.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace editor {

// Desenhadores do Inspector para tipos de campo definidos fora da engine (motor::FieldTraits<T>, ver
// motor/scene/Component.h). Cada tipo registra o seu num .cpp próprio, sem mexer no InspectorPanel:
//
//   MOTOR_FIELD_DRAWER(LightKind) {
//       auto& kind = *static_cast<LightKind*>(value);
//       ...ImGui::Combo("##v", ...)...
//   }
//
// O nome é o CustomFieldType::name do tipo. O widget usa o id "##v" (o rótulo já foi desenhado). `field` traz
// as opções do campo (FieldOptions). Sem desenhador, o Inspector mostra o campo como somente leitura.
using FieldDrawerFn = void (*)(EditorContext& ctx, const motor::FieldInfo& field, void* value);

struct FieldDrawerRegistrar {
    FieldDrawerRegistrar(const char* typeName, FieldDrawerFn fn);
};

FieldDrawerFn FindFieldDrawer(std::string_view typeName);

// Combo de asset para desenhadores: embutidos primeiro (malhas e texturas), depois os arquivos do projeto
// desse tipo. Aceita arquivos arrastados do painel Projeto. `current`: chave atual (nulo = nenhum);
// `noneLabel`: item "nenhum" (nulo = não oferece). Devolve a chave escolhida ("" = nenhum).
std::optional<std::string> AssetCombo(EditorContext& ctx, const char* id, motor::AssetType type,
                                      const std::string* current, const char* noneLabel = "(nenhuma)");

#define MOTOR_FIELD_DRAWER(TypeName)                                                                         \
    static void MotorFieldDrawer_##TypeName(::editor::EditorContext& ctx, const ::motor::FieldInfo& field, \
                                            void* value);                                                  \
    static const ::editor::FieldDrawerRegistrar g_motorFieldDrawer_##TypeName(#TypeName,                  \
                                                                              &MotorFieldDrawer_##TypeName); \
    static void MotorFieldDrawer_##TypeName([[maybe_unused]] ::editor::EditorContext& ctx,                \
                                            [[maybe_unused]] const ::motor::FieldInfo& field,              \
                                            [[maybe_unused]] void* value)

} // namespace editor
