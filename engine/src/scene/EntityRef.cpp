#include "motor/scene/EntityRef.h"

#include "motor/scene/Scene.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace motor {

// Na cena: o id da entidade (número), ou null para nenhuma.
CustomFieldType FieldTraits<EntityRef>::Describe() {
    return {kEntityRefTypeName,
            [](const void* a, const void* b) { return *static_cast<const EntityRef*>(a) == *static_cast<const EntityRef*>(b); },
            [](const void* v, const AssetLibrary&) {
                const auto& ref = *static_cast<const EntityRef*>(v);
                return ref.IsEmpty() ? std::string("null") : std::to_string(ref.id);
            },
            [](std::string_view text, void* v, AssetLibrary&, std::string* error) {
                const auto json = nlohmann::json::parse(text, nullptr, false);
                auto& ref = *static_cast<EntityRef*>(v);
                if (json.is_null()) {
                    ref = {};
                    return true;
                }
                if (json.is_number_unsigned() || (json.is_number_integer() && json.get<long long>() >= 0)) {
                    ref.id = json.get<EntityId>();
                    return true;
                }
                if (error) *error = "esperado o id da entidade (número, ex.: 3) ou null";
                return false;
            }};
}

void RemapEntityRefs(Entity& entity, const std::function<EntityId(EntityId)>& map) {
    for (ComponentPtr& component : entity.components) {
        for (const FieldInfo& field : component.Type().fields) {
            if (field.type != FieldType::Custom || !field.custom || field.custom->name != kEntityRefTypeName) continue;
            auto& ref = field.As<EntityRef>(component.Data());
            if (!ref.IsEmpty()) ref.id = map(ref.id);
        }
    }
}

} // namespace motor
