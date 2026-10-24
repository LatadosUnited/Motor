// Ferramentas de navegação do MCP (Fase 34): gerar o navmesh, pedir um caminho e ver os agentes. Como as de cena,
// usam só o EditorContext e o jogo rodando (RuntimeGetter): funcionam sem janela (test_mcp_nav).

#include "McpSceneTools.h"

#include "NavMeshBake.h"

#include <motor/ai/NavMesh.h>
#include <motor/ai/NavSystem.h>
#include <motor/scene/NavMeshAgent.h>
#include <motor/scene/NavMeshSurface.h>
#include <motor/scene/SceneRuntime.h>

#include <format>
#include <memory>

namespace editor::mcp {

using namespace motor;

namespace {

const Json kPointProp = {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}};

Json Point(const glm::vec3& v) { return Json::array({v.x, v.y, v.z}); }

Json PointProp(const char* description) {
    Json p = kPointProp;
    p["description"] = description;
    return p;
}

glm::vec3 ReadPoint(const Json& args, const char* key) {
    if (!args.contains(key)) throw ToolError(std::format("informe '{}' ([x, y, z] no mundo)", key));
    const Json& v = args[key];
    if (!v.is_array() || v.size() != 3 || !v[0].is_number() || !v[1].is_number() || !v[2].is_number()) {
        throw ToolError(std::format("'{}' deve ser uma lista de 3 números, ex.: [0, 0, -5]", key));
    }
    return {v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
}

NavSystem* RunningNav(const RuntimeGetter& runtime) {
    SceneRuntime* r = runtime ? runtime() : nullptr;
    return r ? r->GetSystem<NavSystem>() : nullptr;
}

Json Corners(const std::vector<glm::vec3>& corners) {
    Json out = Json::array();
    for (const glm::vec3& c : corners) out.push_back(Point(c));
    return out;
}

} // namespace

void AddNavTools(Protocol& protocol, EditorContext& ctx, RuntimeGetter runtime) {
    protocol.AddTool({
        .name = "nav_bake",
        .description = "Gera o navmesh da cena (Colliders estáticos das entidades ativas) com os parâmetros da "
                       "Superfície de navegação e grava no .mnav dela (ou num novo em Assets/Navegacao), como o botão "
                       "Gerar do Inspector. Fora do Play. Devolve o arquivo e quantos polígonos saíram.",
        .inputSchema = Schema({{"entity", {{"type", {"integer", "string"}},
                                           {"description", "entidade com a Superfície (padrão: a primeira ativa)"}}}}),
        .run = [&ctx](const Json& args) {
            Entity* e = args.contains("entity") ? &ResolveEntity(ctx, args) : FindNavSurface(ctx);
            NavMeshSurface* surface = e ? e->Get<NavMeshSurface>() : nullptr;
            if (!surface) {
                throw ToolError(e ? std::format("{} não tem Superfície de navegação", e->name)
                                  : "a cena não tem Superfície de navegação (component_add type = NavMeshSurface)");
            }
            const NavBakeResult result = BakeNavMesh(ctx, *surface);
            if (!result.ok) throw ToolError(result.error);
            return ToolResult::Data({{"entity", e->id},
                                     {"file", surface->navMesh.path},
                                     {"guid", surface->navMesh.guid},
                                     {"polygons", result.stats.polygons},
                                     {"triangles", result.stats.triangles},
                                     {"build_ms", result.stats.buildMs}});
        },
    });

    protocol.AddTool({
        .name = "nav_path",
        .description = "Caminho pelo navmesh entre dois pontos do mundo: os cantos, o comprimento e se chega (partial "
                       "= o fim está fora de alcance e o caminho vai até o ponto mais perto). No Play usa o navmesh do "
                       "jogo; fora dele, o .mnav da Superfície ou, sem arquivo, um gerado na hora (sem gravar).",
        .inputSchema = Schema({{"from", PointProp("início [x, y, z]")}, {"to", PointProp("fim [x, y, z]")}},
                              {"from", "to"}),
        .readOnly = true,
        .run = [&ctx, runtime](const Json& args) {
            const glm::vec3 from = ReadPoint(args, "from"), to = ReadPoint(args, "to");
            const NavMesh* nav = nullptr;
            std::unique_ptr<NavMesh> owned;
            std::string source;
            if (NavSystem* system = RunningNav(runtime); system && system->GetNavMesh()) {
                nav = system->GetNavMesh();
                source = "jogo";
            } else {
                const Entity* e = FindNavSurface(ctx);
                const NavMeshSurface* surface = e ? e->Get<NavMeshSurface>() : nullptr;
                if (!surface) throw ToolError("a cena não tem Superfície de navegação (component_add type = NavMeshSurface)");
                std::string error;
                if (!surface->navMesh.file.empty()) {
                    owned = NavMesh::Load(surface->navMesh.file, &error);
                    source = surface->navMesh.path;
                }
                if (!owned) {
                    NavStats stats;
                    owned = BuildSceneNavMesh(ctx.scene, *surface, &stats);
                    if (!owned) throw ToolError("o navmesh não foi gerado: " + stats.error);
                    source = "cena (gerado agora, sem gravar)";
                }
                nav = owned.get();
            }

            std::vector<glm::vec3> corners;
            bool partial = false;
            const bool found = nav->FindPath(from, to, corners, &partial);
            Json out = {{"found", found}, {"source", source}};
            if (!found) {
                out["reason"] = "início ou fim longe do navmesh (mais de 2 m no plano ou 4 m na altura)";
                return ToolResult::Data(out);
            }
            out["partial"] = partial;
            out["length"] = NavMesh::PathLength(corners);
            out["corners"] = Corners(corners);
            return ToolResult::Data(out);
        },
    });

    protocol.AddTool({
        .name = "nav_agents",
        .description = "Agentes de navegação (NavMeshAgent) da cena. No Play: posição dos pés, velocidade, se tem "
                       "caminho, se chegou, quanto falta e os cantos que faltam, e o navmesh em uso (polígonos, se veio "
                       "do .mnav e se está desatualizado). Fora do Play: só os agentes e o arquivo da Superfície.",
        .inputSchema = Schema(),
        .readOnly = true,
        .run = [&ctx, runtime](const Json&) {
            const Scene& scene = ctx.scene;
            NavSystem* nav = RunningNav(runtime);
            Json out = {{"playing", nav != nullptr}};
            if (nav) {
                const NavStats& stats = nav->Stats();
                out["navmesh"] = nav->GetNavMesh()
                                     ? Json{{"polygons", stats.polygons},
                                            {"from_file", stats.fromFile},
                                            {"stale", stats.stale},
                                            {"build_ms", stats.buildMs}}
                                     : Json{{"error", stats.error.empty() ? "sem Superfície de navegação" : stats.error}};
            } else if (const Entity* e = FindNavSurface(ctx)) {
                const NavMeshData& data = e->Get<NavMeshSurface>()->navMesh;
                out["surface"] = {{"entity", e->id}, {"file", data.IsEmpty() ? Json(nullptr) : Json(data.path)}};
            }

            Json agents = Json::array();
            for (EntityId id : scene.EntitiesWith<NavMeshAgent>()) {
                const Entity* e = scene.Find(id);
                if (!e) continue;
                const NavMeshAgent& settings = *e->Get<NavMeshAgent>();
                const glm::vec3 feet = glm::vec3(scene.WorldMatrix(id)[3]) - glm::vec3(0.0f, settings.baseOffset, 0.0f);
                Json agent = {{"id", id}, {"name", e->name}, {"position", Point(feet)}, {"speed", settings.speed}};
                if (!settings.target.IsEmpty()) agent["target"] = settings.target.id;
                if (!scene.IsActiveInHierarchy(id)) agent["active"] = false;
                if (nav) {
                    agent["velocity"] = Point(nav->GetVelocity(id));
                    agent["has_path"] = nav->HasPath(id);
                    agent["arrived"] = nav->HasArrived(id);
                    if (nav->HasPath(id)) {
                        agent["partial"] = nav->IsPathPartial(id);
                        agent["remaining"] = nav->RemainingDistance(id);
                        agent["path"] = Corners(nav->GetPath(id));
                    }
                }
                agents.push_back(std::move(agent));
            }
            out["agents"] = std::move(agents);
            return ToolResult::Data(out);
        },
    });
}

} // namespace editor::mcp
