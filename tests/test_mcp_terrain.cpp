// Fase 28, Tarefa 14: ferramentas MCP do terreno e da água (terrain_info, terrain_heights, terrain_blocks e
// water_info), sem janela como o test_mcp. O terreno fica só na memória (sem projeto).

#include "TestUtil.h"

#include <EditorContext.h>
#include <mcp/McpProtocol.h>
#include <mcp/McpSceneTools.h>

#include <motor/assets/TerrainData.h>
#include <motor/scene/Components.h>

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace editor;
using namespace editor::mcp;

namespace {
Json Call(Protocol& protocol, const char* tool, Json args = Json::object()) {
    static int id = 100;
    const auto response = protocol.Handle(Json{{"jsonrpc", "2.0"},
                                               {"id", ++id},
                                               {"method", "tools/call"},
                                               {"params", {{"name", tool}, {"arguments", std::move(args)}}}});
    if (!response || !response->contains("result")) return Json{{"rpc_error", response ? (*response)["error"] : Json()}};
    const Json& result = (*response)["result"];
    Json out = {{"isError", result["isError"]}};
    if (!result["content"].empty() && result["content"][0]["type"] == "text") {
        out["text"] = result["content"][0]["text"];
        out["data"] = Json::parse(result["content"][0]["text"].get<std::string>(), nullptr, false);
    }
    return out;
}

bool Contains(const Json& result, const char* text) {
    return result.contains("text") && result["text"].get<std::string>().find(text) != std::string::npos;
}

bool Near(double a, double b, double eps = 1e-3) { return std::abs(a - b) <= eps; }
} // namespace

int main() {
    EditorContext ctx;
    // Desfazer como no editor: a pilha de passos (desfazer, refazer).
    std::vector<std::pair<std::function<void()>, std::function<void()>>> undo;
    ctx.pushSceneUndo = [&undo](std::function<void()> u, std::function<void()> r) { undo.emplace_back(std::move(u), std::move(r)); };
    Protocol protocol("teste", "1.0");
    AddSceneTools(protocol, ctx);
    AddTerrainTools(protocol, ctx);

    // Terreno de 2 x 2 blocos de 32 m (de -32 a 32), com a entidade 5 m acima.
    auto data = std::make_shared<motor::TerrainData>();
    for (int z = -1; z < 1; ++z) {
        for (int x = -1; x < 1; ++x) data->AddBlock({x, z});
    }
    motor::Entity& ground = ctx.scene.CreateEntity("Chão");
    ground.transform.position = {0.0f, 5.0f, 0.0f};
    ground.Add<motor::Terrain>().terrain.SetData(data);

    SECTION("terrain_info: blocos, caixa e alturas sob pontos; erros claros");
    {
        data->SetSample(3, 4, 2.0f);
        const Json info = Call(protocol, "terrain_info", {{"entity", "Chão"}, {"points", {{3.0, 4.0}, {100.0, 0.0}, {-10.0, 7.5}}}});
        CHECK(info["isError"] == false);
        const Json& d = info["data"];
        CHECK(d["block_count"] == 4 && d["blocks"].size() == 4 && Near(d["spacing"].get<double>(), 1.0));
        CHECK(Near(d["bounds"]["min"][0].get<double>(), -32.0) && Near(d["bounds"]["max"][1].get<double>(), 7.0));
        CHECK(Near(d["points"][0]["height"].get<double>(), 7.0) && d["points"][0]["sample"] == Json::array({3, 4}));
        CHECK(d["points"][1].is_null());
        CHECK(Near(d["points"][2]["height"].get<double>(), 5.0) && Near(d["points"][2]["slope"].get<double>(), 0.0, 0.01));
        CHECK(d["points"][2]["weights"][0] == 1.0);
        CHECK(Contains(Call(protocol, "terrain_info", {{"entity", "Ninguém"}}), "Ninguém"));
        ctx.scene.CreateEntity("Vazio");
        CHECK(Contains(Call(protocol, "terrain_info", {{"entity", "Vazio"}}), "não tem Terreno"));
    }

    SECTION("terrain_heights: ler, gravar número e linhas, somar, desfazer e refazer");
    {
        const Json read = Call(protocol, "terrain_heights", {{"entity", "Chão"}, {"from", {2, 4}}, {"size", {3, 1}}});
        CHECK(read["isError"] == false && read["data"]["heights"] == Json::parse("[[0.0, 2.0, 0.0]]"));
        // Fora dos blocos: null.
        const Json outside = Call(protocol, "terrain_heights", {{"entity", "Chão"}, {"from", {31, 0}}, {"size", {3, 1}}});
        CHECK(outside["data"]["heights"][0][2].is_null() && !outside["data"]["heights"][0][1].is_null());

        undo.clear();
        const Json flat = Call(protocol, "terrain_heights", {{"entity", "Chão"}, {"from", {-2, -2}}, {"size", {4, 4}}, {"heights", 1.5}});
        CHECK(flat["isError"] == false && flat["data"]["changed"] == 16 && flat["data"]["unsaved"] == true);
        CHECK(data->Sample(-2, -2) == 1.5f && data->Sample(1, 1) == 1.5f && data->Sample(2, 2) == 0.0f);
        const Json rows = Call(protocol, "terrain_heights",
                               {{"entity", "Chão"}, {"from", {10, 10}}, {"size", {2, 2}}, {"heights", {{1, 2}, {3, 4}}}, {"mode", "add"}});
        CHECK(rows["data"]["changed"] == 4 && data->Sample(11, 11) == 4.0f && data->Sample(11, 10) == 2.0f);
        CHECK(undo.size() == 2);
        undo[1].first();
        CHECK(data->Sample(11, 11) == 0.0f);
        undo[1].second();
        CHECK(data->Sample(11, 11) == 4.0f);
        // Linhas do tamanho errado e retângulo grande demais: erro, nada muda.
        CHECK(Call(protocol, "terrain_heights", {{"entity", "Chão"}, {"from", {0, 0}}, {"size", {2, 2}}, {"heights", {{1, 2}}}})["isError"] == true);
        CHECK(Call(protocol, "terrain_heights", {{"entity", "Chão"}, {"from", {0, 0}}, {"size", {300, 300}}})["isError"] == true);
        CHECK(undo.size() == 2);
        ctx.playing = true;
        CHECK(Contains(Call(protocol, "terrain_heights", {{"entity", "Chão"}, {"from", {0, 0}}, {"size", {1, 1}}, {"heights", 3}}), "Play"));
        ctx.playing = false;
    }

    SECTION("terrain_blocks: pôr com a borda dos vizinhos, tirar, desfazer devolve as alturas");
    {
        undo.clear();
        data->SetSample(32, 5, 9.0f);  // borda leste do bloco (0, 0)
        const Json added = Call(protocol, "terrain_blocks", {{"entity", "Chão"}, {"action", "add"}, {"rect", {1, -1, 2, 0}}, {"height", 3}});
        CHECK(added["isError"] == false && added["data"]["changed"] == 4 && added["data"]["block_count"] == 8);
        // O bloco (1, 0) começa com a borda do (0, 0): sem degrau; o resto na altura pedida.
        CHECK(data->BlockHeights({1, 0}) && (*data->BlockHeights({1, 0}))[5 * 33 + 0] == 9.0f);
        CHECK(data->Sample(40, 5) == 3.0f && data->Sample(95, 10) == 3.0f);
        // De novo: os que já existem não contam.
        CHECK(Call(protocol, "terrain_blocks", {{"entity", "Chão"}, {"action", "add"}, {"blocks", {{1, 0}}}})["data"]["changed"] == 0);

        data->SetSample(40, 5, 6.0f);
        const Json removed = Call(protocol, "terrain_blocks", {{"entity", "Chão"}, {"action", "remove"}, {"blocks", {{1, 0}, {7, 7}}}});
        CHECK(removed["data"]["changed"] == 1 && !data->HasBlock({1, 0}) && data->BlockCount() == 7);
        CHECK(!data->Sample(40, 5).has_value() && data->Sample(32, 5) == 9.0f);  // a borda continua no vizinho
        undo.back().first();  // desfazer: o bloco volta com as alturas que tinha
        CHECK(data->HasBlock({1, 0}) && data->Sample(40, 5) == 6.0f);
        undo.back().second();
        CHECK(!data->HasBlock({1, 0}));
        undo.front().first();  // desfazer o "add": os 4 saem (o (1, 0) já tinha saído)
        CHECK(data->BlockCount() == 4);
        CHECK(Call(protocol, "terrain_blocks", {{"entity", "Chão"}, {"action", "add"}, {"rect", {0, 0, 100, 100}}})["isError"] == true);
        CHECK(Call(protocol, "terrain_blocks", {{"entity", "Chão"}, {"action", "add"}})["isError"] == true);
    }

    SECTION("water_info e entity_create water: superfície com ondas, fundo e profundidade");
    {
        const Json created = Call(protocol, "entity_create", {{"kind", "water"}, {"name", "Lago"}, {"position", {0, 6, 0}}});
        CHECK(created["isError"] == false);
        motor::Entity* lake = ctx.scene.FindByName("Lago");
        CHECK(lake && lake->Get<motor::Water>());
        if (lake) lake->Get<motor::Water>()->surface.waveHeight = 0.0f;
        const Json still = Call(protocol, "water_info", {{"entity", "Lago"}, {"points", {{5.0, 5.0}, {80.0, 0.0}, {3.0, 4.0}}}});
        CHECK(still["isError"] == false);
        const Json& p = still["data"]["points"];
        CHECK(Near(still["data"]["rest_height"].get<double>(), 6.0) && still["data"]["corners"].size() == 4);
        // Sobre o terreno plano (5 m): 1 m de água; fora do retângulo de 100 m; sobre a amostra a 7 m: o fundo fica acima.
        CHECK(p[0]["inside"] == true && Near(p[0]["surface"].get<double>(), 6.0) && Near(p[0]["depth"].get<double>(), 1.0));
        CHECK(p[1]["inside"] == false);
        CHECK(p[2]["dry"] == true && Near(p[2]["bottom"].get<double>(), 7.0) && p[2]["depth"] == 0.0);
        CHECK(p[0]["dry"] == false);
        // Com ondas: a superfície é a mesma conta da CPU (WaveOffset) que o shader faz.
        if (lake) lake->Get<motor::Water>()->surface.waveHeight = 0.5f;
        const Json waves = Call(protocol, "water_info", {{"entity", "Lago"}, {"points", {{5.0, 5.0}}}, {"time", 1.5}});
        const float expected = 6.0f + (lake ? lake->Get<motor::Water>()->surface.WaveOffset({5.0f, 5.0f}, 1.5f).y : 0.0f);
        CHECK(Near(waves["data"]["points"][0]["surface"].get<double>(), expected, 1e-4));
        CHECK(Contains(Call(protocol, "water_info", {{"entity", "Chão"}}), "não tem Água"));
    }
    return TestResult();
}
