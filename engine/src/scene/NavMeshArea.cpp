#include "motor/scene/NavMeshArea.h"

#include <nlohmann/json.hpp>

#include <string>

namespace motor {

CustomFieldType FieldTraits<NavAreaMask>::Describe() {
    return {"NavAreaMask",
            [](const void* a, const void* b) {
                return *static_cast<const NavAreaMask*>(a) == *static_cast<const NavAreaMask*>(b);
            },
            [](const void* v, const AssetLibrary&) { return std::to_string(static_cast<const NavAreaMask*>(v)->bits); },
            [](std::string_view text, void* v, AssetLibrary&, std::string* error) {
                const auto json = nlohmann::json::parse(text, nullptr, false);
                if (json.is_number_integer() && json.get<int64_t>() >= 0 && json.get<int64_t>() <= 0xffff) {
                    static_cast<NavAreaMask*>(v)->bits = static_cast<uint16_t>(json.get<int64_t>());
                    return true;
                }
                if (error) *error = "esperado o número dos bits das áreas, de 0 a 65535";
                return false;
            }};
}

MOTOR_COMPONENT_MODULE(NavArea) {
    registry.Register<NavMeshArea>("NavMeshArea", "Área de navegação")
        .Field("size", "Tamanho", &NavMeshArea::size,
               {.speed = 0.01f, .min = 0.01f, .max = 10000.0f, .format = "%.2f",
                .tooltip = "Caixa que marca o navmesh, no espaço da entidade"})
        .Field("center", "Centro", &NavMeshArea::center, {.speed = 0.01f})
        .Field("area", "Área", &NavMeshArea::area,
               {.speed = 0.05f, .min = 1.0f, .max = static_cast<float>(kNavAreaCount - 1),
                .tooltip = "Número da área (1 a 15); a 0 é o chão comum"})
        .Field("name", "Nome", &NavMeshArea::name, {.tooltip = "Como a área aparece nos agentes (Água, Lama...)"})
        .Field("cost", "Custo", &NavMeshArea::cost,
               {.speed = 0.05f, .min = 1.0f, .max = 1000.0f, .format = "%.2f",
                .tooltip = "Andar 1 m aqui conta como isso na busca de caminho (vale na hora, sem gerar de novo)"});
}

} // namespace motor
