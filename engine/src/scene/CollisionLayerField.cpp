#include "motor/scene/CollisionLayerField.h"

#include <nlohmann/json.hpp>

#include <format>
#include <string>

namespace motor {

CustomFieldType FieldTraits<CollisionLayer>::Describe() {
    return {"CollisionLayer",
            [](const void* a, const void* b) {
                return *static_cast<const CollisionLayer*>(a) == *static_cast<const CollisionLayer*>(b);
            },
            [](const void* v, const AssetLibrary&) {
                return std::to_string(static_cast<const CollisionLayer*>(v)->index);
            },
            [](std::string_view text, void* v, AssetLibrary&, std::string* error) {
                const auto json = nlohmann::json::parse(text, nullptr, false);
                if (json.is_number_integer() && json.get<int>() >= 0 && json.get<int>() < CollisionLayers::kCount) {
                    static_cast<CollisionLayer*>(v)->index = static_cast<uint8_t>(json.get<int>());
                    return true;
                }
                if (error) *error = std::format("esperado o número da camada, de 0 a {}", CollisionLayers::kCount - 1);
                return false;
            }};
}

} // namespace motor
