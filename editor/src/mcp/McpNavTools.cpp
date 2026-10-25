// Ferramentas de navegação do MCP (Fase 34): gerar o navmesh, pedir um caminho e ver os agentes; e a percepção deles
// (Fase 37, ai_perception). Como as de cena, usam só o EditorContext e o jogo rodando (RuntimeGetter): funcionam sem
// janela (test_mcp_nav).

#include "McpSceneTools.h"

#include "NavMeshBake.h"

#include <motor/ai/NavMesh.h>
#include <motor/ai/NavSystem.h>
#include <motor/ai/PerceptionSystem.h>
#include <motor/scene/AudioSource.h>
#include <motor/scene/NavMeshAgent.h>
#include <motor/scene/NavMeshSurface.h>
#include <motor/scene/Perception.h>
#include <motor/scene/PerceptionTarget.h>
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

PerceptionSystem* RunningSenses(const RuntimeGetter& runtime) {
    SceneRuntime* r = runtime ? runtime() : nullptr;
    return r ? r->GetSystem<PerceptionSystem>() : nullptr;
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
        .description = "Agentes de navegação (NavMeshAgent) da cena: posição dos pés, raio e velocidade pedida. No Play "
                       "também: velocidade, se tem caminho, se chegou, quanto falta, os cantos que faltam, se está "
                       "atravessando uma ligação (pulo, escada), os vizinhos de quem desvia (ids, do mais perto ao mais "
                       "longe) e o navmesh em uso (polígonos, ligações, se veio do .mnav e se está desatualizado). Fora "
                       "do Play: só os agentes e o arquivo da Superfície.",
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
                                            {"links", nav->GetNavMesh()->Links().size()},
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
                const glm::vec3 feet = NavAgentFeet(scene, id);
                Json agent = {{"id", id},
                              {"name", e->name},
                              {"position", Point(feet)},
                              {"radius", settings.radius},
                              {"speed", settings.speed}};
                if (!settings.target.IsEmpty()) agent["target"] = settings.target.id;
                if (!scene.IsActiveInHierarchy(id)) agent["active"] = false;
                if (nav) {
                    agent["velocity"] = Point(nav->GetVelocity(id));
                    agent["has_path"] = nav->HasPath(id);
                    agent["arrived"] = nav->HasArrived(id);
                    if (nav->IsOnLink(id)) agent["on_link"] = true;
                    agent["neighbors"] = nav->GetNeighbors(id);
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

    protocol.AddTool({
        .name = "ai_perception",
        .description = "Percepção dos agentes (Perception) e os alvos que eles podem perceber (PerceptionTarget). De cada "
                       "agente: olhos e frente no mundo, alcance e ângulo da visão, alcance em volta, audição, memória "
                       "e os grupos que percebe. No Play também: quem vê agora (id, nome, distância), o que ouviu no "
                       "último passo (fonte, onde, distância) e o que lembra (alvo, última posição, há quantos segundos, "
                       "se veio da visão), e os números do último passo. `entity`: só esse agente.",
        .inputSchema = Schema({{"entity", {{"type", {"integer", "string"}},
                                           {"description", "só este agente (padrão: todos com Percepção)"}}}}),
        .readOnly = true,
        .run = [&ctx, runtime](const Json& args) {
            const Scene& scene = ctx.scene;
            PerceptionSystem* senses = RunningSenses(runtime);
            Json out = {{"playing", senses != nullptr}};
            const auto name = [&](EntityId id) {
                const Entity* e = scene.Find(id);
                return e ? Json(e->name) : Json(nullptr);
            };
            EntityId only = kNullEntity;
            if (args.contains("entity")) {
                const Entity& e = ResolveEntity(ctx, args);
                if (!e.Get<Perception>()) throw ToolError(std::format("{} não tem Percepção", e.name));
                only = e.id;
            }

            Json agents = Json::array();
            for (EntityId id : scene.EntitiesWith<Perception>()) {
                if (only != kNullEntity && id != only) continue;
                const Entity* e = scene.Find(id);
                if (!e) continue;
                const Perception& p = *e->Get<Perception>();
                const PerceptionEye eye = PerceptionEyeOf(scene, id);
                Json agent = {{"id", id},
                              {"name", e->name},
                              {"eye", Point(eye.position)},
                              {"forward", Point(eye.forward)},
                              {"sight_range", p.sightRange},
                              {"sight_angle", p.sightAngle},
                              {"near_range", p.nearRange},
                              {"hearing", p.hearing},
                              {"memory", p.memory},
                              {"detects", p.detects}};
                if (!scene.IsActiveInHierarchy(id)) agent["active"] = false;
                if (senses) {
                    Json seen = Json::array();
                    for (EntityId target : senses->GetSeen(id)) {
                        seen.push_back({{"id", target},
                                        {"name", name(target)},
                                        {"distance", glm::distance(eye.position, PerceptionPointOf(scene, target))}});
                    }
                    Json heard = Json::array();
                    for (const HeardNoise& noise : senses->GetHeard(id)) {
                        Json h = {{"position", Point(noise.position)}, {"distance", noise.distance}};
                        h["source"] = noise.source == kNullEntity ? Json(nullptr) : Json(noise.source);
                        if (noise.source != kNullEntity) h["name"] = name(noise.source);
                        heard.push_back(std::move(h));
                    }
                    Json memory = Json::array();
                    for (const PerceptionMemory& m : senses->GetMemory(id)) {
                        memory.push_back({{"target", m.target},
                                          {"name", name(m.target)},
                                          {"position", Point(m.position)},
                                          {"age", m.age},
                                          {"seen", m.seen},
                                          {"heard", m.heard},
                                          {"by_sight", m.bySight}});
                    }
                    agent["seen"] = std::move(seen);
                    agent["heard"] = std::move(heard);
                    agent["memory"] = std::move(memory);
                }
                agents.push_back(std::move(agent));
            }
            out["agents"] = std::move(agents);

            Json targets = Json::array();
            for (EntityId id : scene.EntitiesWith<PerceptionTarget>()) {
                const Entity* e = scene.Find(id);
                if (!e) continue;
                const PerceptionTarget& t = *e->Get<PerceptionTarget>();
                Json target = {{"id", id},
                               {"name", e->name},
                               {"group", t.group},
                               {"point", Point(PerceptionPointOf(scene, id))},
                               {"visibility", t.visibility},
                               {"sound_range", t.soundRange}};
                if (const AudioSource* source = e->Get<AudioSource>()) target["audible_range"] = t.soundRange * source->volume;
                if (!scene.IsActiveInHierarchy(id)) target["active"] = false;
                targets.push_back(std::move(target));
            }
            out["targets"] = std::move(targets);

            if (senses) {
                const PerceptionStats& s = senses->Stats();
                out["stats"] = {{"perceivers", s.perceivers}, {"targets", s.targets},   {"checks", s.checks},
                                {"raycasts", s.raycasts},     {"deferred", s.deferred}, {"noises", s.noises},
                                {"ms", s.ms},                 {"raycast_budget", senses->RaycastBudget()}};
            }
            return ToolResult::Data(out);
        },
    });
}

} // namespace editor::mcp
