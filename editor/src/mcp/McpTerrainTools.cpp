// Ferramentas do terreno e da água no MCP (Fase 28, Tarefa 14): ver o terreno e as alturas em pontos (terrain_info),
// ler e gravar um retângulo de amostras (terrain_heights), aumentar e diminuir o terreno por blocos (terrain_blocks) e
// a altura e a profundidade da água em pontos (water_info). O pincel (terrain_brush) fica com as de cena. Como elas,
// usam só o EditorContext: funcionam sem janela (test_mcp_terrain). Mudanças viram um passo de desfazer.

#include "McpSceneTools.h"

#include <motor/assets/TerrainBrush.h>
#include <motor/renderer/ShadowMath.h>
#include <motor/scene/Terrain.h>
#include <motor/scene/Water.h>

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <vector>

namespace editor::mcp {

using namespace motor;

namespace {

const Json kEntityProp = {{"type", {"integer", "string"}}, {"description", "id da entidade ou o nome dela"}};
const Json kPointsProp = {{"type", "array"},
                          {"items", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 2}, {"maxItems", 2}}},
                          {"description", "pontos [x, z] do mundo"}};
constexpr size_t kMaxSamples = 257 * 257;  // terrain_heights: um retângulo por chamada
constexpr size_t kMaxBlocks = 4096;        // terrain_blocks: 64 x 64 blocos (2 x 2 km com blocos de 32 m)

Json Vec(const glm::vec3& v) { return Json::array({v.x, v.y, v.z}); }

std::vector<glm::vec2> ReadPoints(const Json& args) {
    std::vector<glm::vec2> points;
    if (!args.contains("points")) return points;
    if (!args["points"].is_array()) throw ToolError("'points' deve ser uma lista de [x, z]");
    for (const Json& p : args["points"]) {
        if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) {
            throw ToolError("cada ponto de 'points' é [x, z], ex.: [10, -4]");
        }
        points.emplace_back(p[0].get<float>(), p[1].get<float>());
    }
    if (points.size() > 4096) throw ToolError("no máximo 4096 pontos por chamada");
    return points;
}

std::pair<int, int> ReadPair(const Json& args, const char* key) {
    const Json& v = args[key];
    if (!v.is_array() || v.size() != 2 || !v[0].is_number_integer() || !v[1].is_number_integer()) {
        throw ToolError(std::format("'{}' deve ser [x, z] inteiros", key));
    }
    return {v[0].get<int>(), v[1].get<int>()};
}

// Terreno da entidade, com as alturas abertas.
std::pair<Entity*, std::shared_ptr<TerrainData>> ResolveTerrain(EditorContext& ctx, const Json& args) {
    Entity& e = ResolveEntity(ctx, args);
    const Terrain* terrain = e.Get<Terrain>();
    if (!terrain) throw ToolError(std::format("{} não tem Terreno", e.name));
    std::shared_ptr<TerrainData> data = terrain->terrain.SharedData();
    if (!data) throw ToolError(std::format("o Terreno de {} não tem arquivo de alturas", e.name));
    return {&e, std::move(data)};
}

// Caixa do terreno no espaço dele (blocos e alturas).
Aabb LocalBounds(const TerrainData& data) {
    Aabb box{glm::vec3(1e30f), glm::vec3(-1e30f)};
    const float size = data.BlockSize();
    for (const TerrainBlockKey key : data.Blocks()) {
        const auto [lo, hi] = data.BlockHeightRange(key);
        box.min = glm::min(box.min, glm::vec3(static_cast<float>(key.x) * size, lo, static_cast<float>(key.z) * size));
        box.max = glm::max(box.max, glm::vec3(static_cast<float>(key.x + 1) * size, hi, static_cast<float>(key.z + 1) * size));
    }
    return box;
}

Json BoundsJson(const Aabb& box) { return {{"min", Vec(box.min)}, {"max", Vec(box.max)}}; }

Json TerrainSummary(const Scene& scene, const Entity& e, const TerrainData& data) {
    const glm::mat4 world = scene.WorldMatrix(e.id);
    Json summary = {{"entity", e.id},
                    {"block_size", data.BlockSize()},
                    {"resolution", data.Resolution()},
                    {"spacing", data.Spacing()},
                    {"block_count", data.BlockCount()},
                    {"unsaved", data.IsDirty()}};
    if (data.BlockCount() > 0) summary["bounds"] = BoundsJson(TransformAabb(LocalBounds(data), world));
    return summary;
}

// Passo de desfazer (se o editor deu um; nos testes sem, só aplica).
void PushUndo(EditorContext& ctx, std::function<void()> undo, std::function<void()> redo) {
    if (ctx.pushSceneUndo) ctx.pushSceneUndo(std::move(undo), std::move(redo));
}

void AddTerrainInfoTool(Protocol& protocol, EditorContext& ctx) {
    protocol.AddTool({
        .name = "terrain_info",
        .description =
            "Mostra o terreno de uma entidade: tamanho e resolução dos blocos, metros entre amostras (spacing), "
            "blocos ([x, z]: o bloco cobre de x * block_size a (x + 1) * block_size no espaço do terreno), caixa no "
            "mundo e se há alturas não salvas. Com points ([x, z] do mundo), a altura do terreno sob cada ponto (no "
            "mundo), a inclinação em graus, os pesos das camadas 1 a 4 e a amostra mais próxima [gx, gz] (para o "
            "terrain_heights); null fora do terreno.",
        .inputSchema = Schema({{"entity", kEntityProp}, {"points", kPointsProp}}, {"entity"}),
        .run = [&ctx](const Json& args) {
            auto [e, data] = ResolveTerrain(ctx, args);
            Json out = TerrainSummary(ctx.scene, *e, *data);
            Json blocks = Json::array();
            for (const TerrainBlockKey key : data->Blocks()) {
                if (blocks.size() >= 1024) break;
                blocks.push_back(Json::array({key.x, key.z}));
            }
            out["blocks"] = std::move(blocks);
            if (data->BlockCount() > 1024) out["blocks_note"] = "só os primeiros 1024 blocos";

            const std::vector<glm::vec2> points = ReadPoints(args);
            if (!points.empty()) {
                const glm::mat4 world = ctx.scene.WorldMatrix(e->id);
                const glm::mat4 toLocal = glm::inverse(world);
                const glm::mat3 normalToWorld = glm::transpose(glm::mat3(toLocal));
                const float top = TransformAabb(LocalBounds(*data), world).max.y + 10.0f;
                const glm::vec3 down = glm::normalize(glm::mat3(toLocal) * glm::vec3(0.0f, -1.0f, 0.0f));
                Json samples = Json::array();
                for (const glm::vec2& p : points) {
                    const glm::vec3 origin = glm::vec3(toLocal * glm::vec4(p.x, top, p.y, 1.0f));
                    const auto hit = data->Raycast(origin, down, 1e6f);
                    if (!hit) {
                        samples.push_back(nullptr);
                        continue;
                    }
                    const float spacing = data->Spacing();
                    const int gx = static_cast<int>(std::lround(hit->x / spacing));
                    const int gz = static_cast<int>(std::lround(hit->z / spacing));
                    const glm::vec3 normal = glm::normalize(normalToWorld * data->NormalAt(gx, gz));
                    const glm::vec4 w = data->Weights(gx, gz).value_or(glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
                    samples.push_back({{"height", (world * glm::vec4(*hit, 1.0f)).y},
                                       {"slope", glm::degrees(std::acos(std::clamp(normal.y, -1.0f, 1.0f)))},
                                       {"weights", Json::array({w.x, w.y, w.z, w.w})},
                                       {"sample", Json::array({gx, gz})}});
                }
                out["points"] = std::move(samples);
            }
            return ToolResult::Data(out);
        },
    });
}

void AddTerrainHeightsTool(Protocol& protocol, EditorContext& ctx) {
    protocol.AddTool({
        .name = "terrain_heights",
        .description =
            "Lê ou grava as alturas de um retângulo de amostras do terreno. from: [gx, gz] da primeira amostra (a "
            "amostra (gx, gz) fica em (gx, gz) * spacing no espaço do terreno; terrain_info dá o spacing e a amostra "
            "sob um ponto); size: [largura, profundidade] em amostras (até 257 x 257). Sem heights, devolve as "
            "alturas (linhas em z, cada uma com as colunas em x; null fora dos blocos). Com heights (um número para "
            "o retângulo todo, ou linhas como as de leitura), grava: mode set (padrão) põe a altura, add soma. "
            "Alturas no espaço do terreno (o mundo soma a posição Y da entidade, com a escala dela). Gravar é um "
            "passo de desfazer e marca a cena como não salva; amostras fora dos blocos são ignoradas.",
        .inputSchema = Schema({{"entity", kEntityProp},
                               {"from", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"minItems", 2}, {"maxItems", 2}}},
                               {"size", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"minItems", 2}, {"maxItems", 2}}},
                               {"heights", {{"description", "número ou lista de linhas (opcional: sem, só lê)"}}},
                               {"mode", {{"type", "string"}, {"enum", {"set", "add"}}}}},
                              {"entity", "from", "size"}),
        .run = [&ctx](const Json& args) {
            auto [e, data] = ResolveTerrain(ctx, args);
            const auto [gx0, gz0] = ReadPair(args, "from");
            const auto [w, d] = ReadPair(args, "size");
            if (w < 1 || d < 1) throw ToolError("'size' precisa de pelo menos 1 x 1 amostra");
            if (static_cast<size_t>(w) * static_cast<size_t>(d) > kMaxSamples) {
                throw ToolError("no máximo 257 x 257 amostras por chamada");
            }
            if (!args.contains("heights")) {
                Json rows = Json::array();
                for (int z = 0; z < d; ++z) {
                    Json row = Json::array();
                    for (int x = 0; x < w; ++x) {
                        const auto h = data->Sample(gx0 + x, gz0 + z);
                        row.push_back(h ? Json(*h) : Json(nullptr));
                    }
                    rows.push_back(std::move(row));
                }
                return ToolResult::Data({{"from", Json::array({gx0, gz0})}, {"size", Json::array({w, d})},
                                         {"spacing", data->Spacing()}, {"heights", std::move(rows)}});
            }
            if (ctx.playing) throw ToolError("pare o Play antes de mexer no terreno");
            const Json& heights = args["heights"];
            const bool add = args.value("mode", "set") == "add";
            auto value = [&](int x, int z) -> float {
                if (heights.is_number()) return heights.get<float>();
                return heights[static_cast<size_t>(z)][static_cast<size_t>(x)].get<float>();
            };
            if (!heights.is_number()) {
                bool ok = heights.is_array() && heights.size() == static_cast<size_t>(d);
                for (size_t z = 0; ok && z < heights.size(); ++z) {
                    ok = heights[z].is_array() && heights[z].size() == static_cast<size_t>(w) &&
                         std::all_of(heights[z].begin(), heights[z].end(), [](const Json& v) { return v.is_number(); });
                }
                if (!ok) throw ToolError(std::format("'heights' deve ser um número ou {} linhas de {} números", d, w));
            }
            auto edit = std::make_shared<TerrainEdit>();
            for (int z = 0; z < d; ++z) {
                for (int x = 0; x < w; ++x) {
                    const auto before = data->Sample(gx0 + x, gz0 + z);
                    if (!before) continue;
                    const float after = add ? *before + value(x, z) : value(x, z);
                    if (after != *before) edit->heights.push_back({gx0 + x, gz0 + z, *before, after});
                }
            }
            ApplyTerrainEdit(*data, *edit, true);
            if (!edit->Empty()) {
                PushUndo(ctx, [data, edit] { ApplyTerrainEdit(*data, *edit, false); },
                         [data, edit] { ApplyTerrainEdit(*data, *edit, true); });
            }
            Json out = TerrainSummary(ctx.scene, *e, *data);
            out["changed"] = edit->heights.size();
            return ToolResult::Data(out);
        },
    });
}

// Blocos tirados: alturas e pesos, para o desfazer pôr de volta.
struct SavedBlock {
    TerrainBlockKey key;
    std::vector<float> heights;
    std::vector<glm::vec4> weights;
};

void RestoreBlock(TerrainData& data, const SavedBlock& block) {
    data.AddBlock(block.key);
    const int res = data.Resolution(), n = res - 1;
    for (int j = 0; j < res; ++j) {
        for (int i = 0; i < res; ++i) {
            const size_t k = static_cast<size_t>(j * res + i);
            data.SetSample(block.key.x * n + i, block.key.z * n + j, block.heights[k]);
            data.SetWeights(block.key.x * n + i, block.key.z * n + j, block.weights[k]);
        }
    }
}

void AddTerrainBlocksTool(Protocol& protocol, EditorContext& ctx) {
    protocol.AddTool({
        .name = "terrain_blocks",
        .description =
            "Aumenta ou diminui o terreno por blocos. action add põe blocos planos na altura `height` (padrão 0; a "
            "borda com os blocos que já existem fica igual à deles), remove tira. Os blocos vêm em blocks ([[x, z], "
            "...]) e/ou rect ([x0, z0, x1, z1], com as pontas), até 4096 por chamada: um terreno de 1 x 1 km com "
            "blocos de 32 m é rect [-16, -16, 15, 15]. Um passo de desfazer; salvar a cena grava o terreno. Devolve "
            "quantos mudaram e o terreno depois.",
        .inputSchema = Schema({{"entity", kEntityProp},
                               {"action", {{"type", "string"}, {"enum", {"add", "remove"}}}},
                               {"blocks", {{"type", "array"},
                                           {"items", {{"type", "array"}, {"items", {{"type", "integer"}}},
                                                      {"minItems", 2}, {"maxItems", 2}}}}},
                               {"rect", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"minItems", 4}, {"maxItems", 4}}},
                               {"height", Prop("number", "add: altura dos blocos novos (espaço do terreno)")}},
                              {"entity", "action"}),
        .run = [&ctx](const Json& args) {
            if (ctx.playing) throw ToolError("pare o Play antes de mexer no terreno");
            auto [e, data] = ResolveTerrain(ctx, args);
            const std::string action = args.value("action", "");
            if (action != "add" && action != "remove") throw ToolError("action é add ou remove");
            std::vector<TerrainBlockKey> keys;
            if (args.contains("blocks")) {
                if (!args["blocks"].is_array()) throw ToolError("'blocks' deve ser uma lista de [x, z]");
                for (const Json& b : args["blocks"]) {
                    const auto [x, z] = ReadPair(Json{{"b", b}}, "b");
                    keys.push_back({x, z});
                }
            }
            if (args.contains("rect")) {
                const Json& r = args["rect"];
                if (!r.is_array() || r.size() != 4 || !std::all_of(r.begin(), r.end(), [](const Json& v) { return v.is_number_integer(); })) {
                    throw ToolError("'rect' deve ser [x0, z0, x1, z1] inteiros");
                }
                const int x0 = std::min(r[0].get<int>(), r[2].get<int>()), x1 = std::max(r[0].get<int>(), r[2].get<int>());
                const int z0 = std::min(r[1].get<int>(), r[3].get<int>()), z1 = std::max(r[1].get<int>(), r[3].get<int>());
                if (static_cast<size_t>(x1 - x0 + 1) * static_cast<size_t>(z1 - z0 + 1) > kMaxBlocks) {
                    throw ToolError("no máximo 4096 blocos por chamada");
                }
                for (int z = z0; z <= z1; ++z) {
                    for (int x = x0; x <= x1; ++x) keys.push_back({x, z});
                }
            }
            if (keys.empty()) throw ToolError("informe 'blocks' ou 'rect'");
            if (keys.size() > kMaxBlocks) throw ToolError("no máximo 4096 blocos por chamada");
            std::sort(keys.begin(), keys.end());
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

            size_t changed = 0;
            if (action == "add") {
                const float height = args.value("height", 0.0f);
                auto added = std::make_shared<std::vector<TerrainBlockKey>>();
                for (const TerrainBlockKey key : keys) {
                    if (data->AddBlock(key, height)) added->push_back(key);
                }
                changed = added->size();
                if (changed > 0) {
                    PushUndo(ctx, [data, added] { for (const TerrainBlockKey k : *added) data->RemoveBlock(k); },
                             [data, added, height] { for (const TerrainBlockKey k : *added) data->AddBlock(k, height); });
                }
            } else {
                auto removed = std::make_shared<std::vector<SavedBlock>>();
                const int res = data->Resolution(), n = res - 1;
                for (const TerrainBlockKey key : keys) {
                    const std::vector<float>* heights = data->BlockHeights(key);
                    if (!heights) continue;
                    SavedBlock saved{key, *heights, {}};
                    saved.weights.reserve(heights->size());
                    for (int j = 0; j < res; ++j) {
                        for (int i = 0; i < res; ++i) {
                            saved.weights.push_back(data->Weights(key.x * n + i, key.z * n + j).value_or(glm::vec4(1, 0, 0, 0)));
                        }
                    }
                    removed->push_back(std::move(saved));
                }
                for (const SavedBlock& b : *removed) data->RemoveBlock(b.key);
                changed = removed->size();
                if (changed > 0) {
                    PushUndo(ctx, [data, removed] { for (const SavedBlock& b : *removed) RestoreBlock(*data, b); },
                             [data, removed] { for (const SavedBlock& b : *removed) data->RemoveBlock(b.key); });
                }
            }
            Json out = TerrainSummary(ctx.scene, *e, *data);
            out["changed"] = changed;
            return ToolResult::Data(out);
        },
    });
}

void AddWaterInfoTool(Protocol& protocol, EditorContext& ctx) {
    protocol.AddTool({
        .name = "water_info",
        .description =
            "Mostra a água de uma entidade (componente Água): tamanho, cantos no mundo e a altura parada da superfície. "
            "Com points ([x, z] do mundo), para cada um: se está dentro da água, a altura da superfície com as ondas "
            "no tempo `time` (segundos, padrão 0; a mesma conta do shader), se é seco (terra ou objeto acima da "
            "superfície: dry, profundidade 0), a altura do fundo (o primeiro objeto ou terreno abaixo da superfície, "
            "null se não há) e a profundidade (superfície menos fundo).",
        .inputSchema = Schema({{"entity", kEntityProp}, {"points", kPointsProp},
                               {"time", Prop("number", "tempo das ondas em segundos (padrão 0)")}},
                              {"entity"}),
        .run = [&ctx](const Json& args) {
            Entity& e = ResolveEntity(ctx, args);
            const Water* water = e.Get<Water>();
            if (!water) throw ToolError(std::format("{} não tem Água", e.name));
            const glm::mat4 world = ctx.scene.WorldMatrix(e.id);
            const glm::vec3 origin(world[3]);
            const glm::vec2 half = water->size * 0.5f;
            Json corners = Json::array();
            for (const glm::vec2 c : {glm::vec2(-half.x, -half.y), glm::vec2(half.x, -half.y), glm::vec2(half.x, half.y),
                                      glm::vec2(-half.x, half.y)}) {
                corners.push_back(Vec(glm::vec3(world * glm::vec4(c.x, 0.0f, c.y, 1.0f))));
            }
            Json out = {{"entity", e.id},
                        {"size", Json::array({water->size.x, water->size.y})},
                        {"corners", std::move(corners)},
                        {"rest_height", origin.y},
                        {"wave_height", water->surface.waveHeight}};
            const std::vector<glm::vec2> points = ReadPoints(args);
            if (!points.empty()) {
                const float time = args.value("time", 0.0f);
                const glm::mat4 toLocal = glm::inverse(world);
                Json samples = Json::array();
                for (const glm::vec2& p : points) {
                    // A superfície parada sob o ponto (a água pode estar girada) e as ondas, no espaço do shader:
                    // posição em relação à entidade, nos eixos do mundo.
                    const glm::vec3 local = glm::vec3(toLocal * glm::vec4(p.x, origin.y, p.y, 1.0f));
                    const bool inside = std::abs(local.x) <= half.x && std::abs(local.z) <= half.y;
                    const float rest = (world * glm::vec4(local.x, 0.0f, local.z, 1.0f)).y;
                    const float surface = rest + water->surface.WaveOffset(p - glm::vec2(origin.x, origin.z), time).y;
                    Json sample = {{"inside", inside}, {"surface", surface}, {"dry", false}, {"bottom", nullptr},
                                   {"depth", nullptr}};
                    // De cima primeiro: terra (ou objeto) acima da superfície = seco. Senão, o fundo é o primeiro que
                    // o raio acha descendo da superfície (os raios não acertam o terreno por baixo).
                    constexpr float kAbove = 10000.0f;
                    const auto top = ctx.scene.Raycast({p.x, surface + kAbove, p.y}, {0.0f, -1.0f, 0.0f});
                    if (top && top->distance <= kAbove) {
                        sample["dry"] = true;
                        sample["bottom"] = surface + kAbove - top->distance;
                        sample["depth"] = 0.0f;
                    } else if (const auto hit = ctx.scene.Raycast({p.x, surface, p.y}, {0.0f, -1.0f, 0.0f})) {
                        sample["bottom"] = surface - hit->distance;
                        sample["depth"] = hit->distance;
                    }
                    samples.push_back(std::move(sample));
                }
                out["points"] = std::move(samples);
            }
            return ToolResult::Data(out);
        },
    });
}

} // namespace

void AddTerrainTools(Protocol& protocol, EditorContext& ctx) {
    AddTerrainInfoTool(protocol, ctx);
    AddTerrainHeightsTool(protocol, ctx);
    AddTerrainBlocksTool(protocol, ctx);
    AddWaterInfoTool(protocol, ctx);
}

} // namespace editor::mcp
