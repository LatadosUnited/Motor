#include "motor/scene/NavMeshSurface.h"

#include "motor/assets/AssetLibrary.h"

#include <nlohmann/json.hpp>

namespace motor {

// Na cena: {"guid": "...", "path": "Assets/Navegacao/x.mnav"}, ou null sem navmesh salvo. Ao ler, o GUID manda: o
// caminho é atualizado se o arquivo mudou de lugar (com o .meta). GUID desconhecido: vale o caminho.
CustomFieldType FieldTraits<NavMeshData>::Describe() {
    return {"NavMeshData",
            [](const void* a, const void* b) { return *static_cast<const NavMeshData*>(a) == *static_cast<const NavMeshData*>(b); },
            [](const void* v, const AssetLibrary&) {
                const auto& data = *static_cast<const NavMeshData*>(v);
                if (data.IsEmpty()) return std::string("null");
                return nlohmann::ordered_json{{"guid", data.guid}, {"path", data.path}}.dump();
            },
            [](std::string_view text, void* v, AssetLibrary& assets, std::string* error) {
                const auto json = nlohmann::json::parse(text, nullptr, false);
                auto& data = *static_cast<NavMeshData*>(v);
                if (json.is_null()) {
                    data = {};
                    return true;
                }
                if (json.is_string()) {
                    // Só o caminho ("Assets/..."), como escreve um agente pelo MCP: o GUID vem do .meta.
                    data = {};
                    data.path = json.get<std::string>();
                    data.file = assets.FileForKey(data.path);
                    std::error_code ec;
                    if (!data.file.empty() && std::filesystem::is_regular_file(data.file, ec)) {
                        data.guid = assets.Database().GuidFor(data.file);
                    }
                    return true;
                }
                if (!json.is_object() || (!json.contains("guid") && !json.contains("path"))) {
                    if (error) *error = "esperado caminho \"Assets/...\", objeto {\"path\": \"Assets/...\"} ou null";
                    return false;
                }
                data.guid = json.value("guid", std::string());
                data.path = json.value("path", std::string());
                data.file.clear();
                if (!data.guid.empty()) data.file = assets.Database().PathFor(data.guid);
                if (!data.file.empty()) {
                    if (const std::string key = assets.KeyForFile(data.file); !key.empty()) data.path = key;
                } else if (!data.path.empty()) {
                    data.file = assets.FileForKey(data.path);
                }
                return true;
            }};
}

// "Nav..." deixa os dois componentes de navegação juntos no menu "Adicionar componente".
MOTOR_COMPONENT_MODULE(NavSurface) {
    registry.Register<NavMeshSurface>("NavMeshSurface", "Superfície de navegação")
        .Field("navMesh", "Navmesh", &NavMeshSurface::navMesh,
               {.tooltip = "Arquivo .mnav gerado pelo botão Gerar navmesh. Vazio: o navmesh é gerado a cada Play"})
        .Field("agentRadius", "Raio do agente", &NavMeshSurface::agentRadius,
               {.speed = 0.01f, .min = 0.05f, .max = 50.0f, .format = "%.2f m",
                .tooltip = "Distância que o navmesh mantém das paredes"})
        .Field("agentHeight", "Altura do agente", &NavMeshSurface::agentHeight,
               {.speed = 0.01f, .min = 0.1f, .max = 100.0f, .format = "%.2f m",
                .tooltip = "Vão livre mínimo: por baixo de algo mais baixo o agente não passa"})
        .Field("agentMaxClimb", "Altura do degrau", &NavMeshSurface::agentMaxClimb,
               {.speed = 0.01f, .min = 0.0f, .max = 10.0f, .format = "%.2f m", .tooltip = "Degrau mais alto que o agente sobe"})
        .Field("agentMaxSlope", "Inclinação máxima", &NavMeshSurface::agentMaxSlope,
               {.slider = true, .min = 0.0f, .max = 89.0f, .format = "%.0f°", .tooltip = "Rampa mais íngreme que o agente sobe"})
        .Field("cellSize", "Tamanho da célula", &NavMeshSurface::cellSize,
               {.speed = 0.005f, .min = 0.02f, .max = 5.0f, .format = "%.3f m",
                .tooltip = "Menor = navmesh mais fiel às bordas e geração mais lenta. Bom valor: metade do raio"})
        .Field("cellHeight", "Altura da célula", &NavMeshSurface::cellHeight,
               {.speed = 0.005f, .min = 0.02f, .max = 5.0f, .format = "%.3f m", .tooltip = "Bom valor: metade do tamanho da célula"})
        .Field("minRegionArea", "Área mínima", &NavMeshSurface::minRegionArea,
               {.speed = 0.1f, .min = 0.0f, .max = 1000.0f, .format = "%.1f m²",
                .tooltip = "Pedaços de chão menores que isso somem (topo de mesa, de caixa)"});
}

} // namespace motor
