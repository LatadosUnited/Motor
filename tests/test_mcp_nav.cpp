// Ferramentas de navegação do MCP (Fase 34): nav_bake (o mesmo que o botão Gerar do Inspector), nav_path (no Play,
// pelo .mnav ou gerado na hora) e nav_agents (antes e durante o Play; Fase 35: raio, vizinhos, ligações). Sem janela,
// como o test_mcp.
#include "TestUtil.h"

#include <EditorContext.h>
#include <mcp/McpProtocol.h>
#include <mcp/McpSceneTools.h>

#include <motor/ai/NavSystem.h>
#include <motor/core/Paths.h>
#include <motor/project/Project.h>
#include <motor/scene/Components.h>
#include <motor/scene/SceneRuntime.h>

#include <string>

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
} // namespace

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    const fs::path base = fs::absolute(TestDir(argc, argv, "test_mcp_nav_tmp")).lexically_normal();
    std::string error;
    auto project = motor::Project::Create(base, "Jogo Ação", &error);
    CHECK(project.has_value());
    if (!project) return TestResult();

    EditorContext ctx;
    ctx.assets.SetRoot(project->RootDir());
    motor::SceneRuntime* running = nullptr;
    Protocol protocol("teste", "1.0");
    AddSceneTools(protocol, ctx, [&running] { return running; });
    AddNavTools(protocol, ctx, [&running] { return running; });

    SECTION("Sem Superfície de navegação: erros que dizem o que fazer");
    {
        const Json bake = Call(protocol, "nav_bake");
        CHECK(bake["isError"] == true && Contains(bake, "NavMeshSurface"));
        CHECK(Call(protocol, "nav_path", {{"from", {0, 0, 0}}, {"to", {1, 0, 0}}})["isError"] == true);
        CHECK(Call(protocol, "nav_path", {{"from", {0, 0}}, {"to", {1, 0, 0}}})["isError"] == true);  // ponto inválido
    }

    // Chão 20 x 20 com uma parede no meio, a superfície e um agente (como no test_navigation).
    motor::Scene& scene = ctx.scene;
    motor::Entity& floor = scene.CreateEntity("Chão");
    floor.transform.position = {0.0f, -0.5f, 0.0f};
    floor.Add<motor::ColliderComponent>().size = {20.0f, 1.0f, 20.0f};
    motor::Entity& wall = scene.CreateEntity("Parede");
    wall.transform.position = {0.0f, 1.5f, 0.0f};
    wall.Add<motor::ColliderComponent>().size = {12.0f, 3.0f, 1.0f};
    const motor::EntityId surfaceId = scene.CreateEntity("Navegação").id;
    scene.Find(surfaceId)->Add<motor::NavMeshSurface>();
    motor::Entity& agent = scene.CreateEntity("Guarda");
    agent.transform.position = {0.0f, 0.0f, -5.0f};
    agent.Add<motor::NavMeshAgent>();
    const motor::EntityId agentId = agent.id;

    SECTION("Fora do Play, sem arquivo: caminho num navmesh gerado na hora, agentes sem estado de jogo");
    {
        const Json path = Call(protocol, "nav_path", {{"from", {0, 0, -5}}, {"to", {0, 0, 5}}});
        CHECK(path["isError"] == false);
        CHECK(path["data"]["found"] == true && path["data"]["partial"] == false);
        CHECK(path["data"]["corners"].size() > 2);
        CHECK(path["data"]["length"].get<float>() > 14.0f);  // dá a volta na parede
        CHECK(path["data"]["source"].get<std::string>().starts_with("cena"));
        CHECK(scene.Find(surfaceId)->Get<motor::NavMeshSurface>()->navMesh.IsEmpty());  // nada gravado

        const Json far = Call(protocol, "nav_path", {{"from", {0, 0, -5}}, {"to", {80, 0, 0}}});
        CHECK(far["isError"] == false && far["data"]["found"] == false && far["data"].contains("reason"));

        const Json agents = Call(protocol, "nav_agents");
        CHECK(agents["data"]["playing"] == false);
        CHECK(agents["data"]["agents"].size() == 1 && agents["data"]["agents"][0]["name"] == "Guarda");
        CHECK(agents["data"]["surface"]["file"].is_null());
        CHECK(!agents["data"]["agents"][0].contains("has_path"));
    }

    SECTION("nav_bake: grava o .mnav, liga à superfície e gera de novo no mesmo arquivo");
    {
        ctx.playing = true;
        const Json duringPlay = Call(protocol, "nav_bake");
        CHECK(duringPlay["isError"] == true && Contains(duringPlay, "Play"));
        ctx.playing = false;

        const Json bake = Call(protocol, "nav_bake", {{"entity", "Navegação"}});
        CHECK(bake["isError"] == false);
        CHECK(bake["data"]["file"] == "Assets/Navegacao/Navmesh.mnav");
        CHECK(bake["data"]["polygons"].get<int>() > 0 && bake["data"]["triangles"] == 24);
        const motor::NavMeshData& data = scene.Find(surfaceId)->Get<motor::NavMeshSurface>()->navMesh;
        CHECK(!data.guid.empty() && data.guid == bake["data"]["guid"].get<std::string>());
        CHECK(fs::exists(project->AssetsDir() / "Navegacao" / "Navmesh.mnav"));
        CHECK(fs::exists(project->AssetsDir() / "Navegacao" / "Navmesh.mnav.meta"));

        // A parede andou: gerar de novo grava por cima do mesmo arquivo (não cria "Navmesh 2").
        scene.FindByName("Parede")->transform.position.x = 2.0f;
        const Json again = Call(protocol, "nav_bake");
        CHECK(again["isError"] == false && again["data"]["file"] == "Assets/Navegacao/Navmesh.mnav");
        CHECK(!fs::exists(project->AssetsDir() / "Navegacao" / "Navmesh 2.mnav"));
        CHECK(Call(protocol, "nav_bake", {{"entity", "Chão"}})["isError"] == true);  // sem superfície

        const Json path = Call(protocol, "nav_path", {{"from", {0, 0, -5}}, {"to", {0, 0, 5}}});
        CHECK(path["data"]["source"] == "Assets/Navegacao/Navmesh.mnav");
        CHECK(Call(protocol, "nav_agents")["data"]["surface"]["file"] == "Assets/Navegacao/Navmesh.mnav");
    }

    SECTION("No Play: navmesh do jogo, agente andando e chegando");
    {
        // Um segundo agente parado ao lado do caminho: o guarda desvia dele (vizinho, Fase 35).
        motor::Entity& other = scene.CreateEntity("Outro");
        other.transform.position = {1.2f, 0.0f, -3.5f};
        other.Add<motor::NavMeshAgent>();
        const motor::EntityId otherId = other.id;
        motor::SceneRuntime runtime(scene);
        runtime.Start();
        running = &runtime;
        motor::NavSystem* nav = runtime.GetSystem<motor::NavSystem>();
        CHECK(nav && nav->SetDestination(agentId, {0.0f, 0.0f, 5.0f}));
        for (int i = 0; i < 30; ++i) runtime.FixedUpdate(1.0f / 60.0f);

        const Json agents = Call(protocol, "nav_agents");
        CHECK(agents["data"]["playing"] == true);
        CHECK(agents["data"]["navmesh"]["from_file"] == true && agents["data"]["navmesh"]["stale"] == false);
        const Json& guard = agents["data"]["agents"][0];
        CHECK(guard["has_path"] == true && guard["arrived"] == false);
        CHECK(guard["remaining"].get<float>() > 5.0f && !guard["path"].empty());
        CHECK(guard["velocity"].size() == 3);
        CHECK(guard["name"] == "Guarda" && guard["radius"] == 0.5f);
        CHECK(guard["neighbors"].is_array() && guard["neighbors"].size() == 1 && guard["neighbors"][0] == otherId);
        CHECK(agents["data"]["navmesh"]["links"] == 0);

        CHECK(Call(protocol, "nav_path", {{"from", {0, 0, -5}}, {"to", {0, 0, 5}}})["data"]["source"] == "jogo");
        CHECK(Call(protocol, "nav_bake")["isError"] == false);  // ctx.playing é do editor; aqui o teste não o liga

        for (int i = 0; i < 60 * 10 && !nav->HasArrived(agentId); ++i) runtime.FixedUpdate(1.0f / 60.0f);
        const Json done = Call(protocol, "nav_agents")["data"]["agents"][0];
        CHECK(done["arrived"] == true && done["has_path"] == false && !done.contains("path"));
        runtime.Stop();
        running = nullptr;
        scene.DestroyEntity(otherId);
    }

    SECTION("Na lista de ferramentas, com as que só leem marcadas");
    {
        const auto list = protocol.Handle(Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}});
        int found = 0;
        for (const Json& tool : (*list)["result"]["tools"]) {
            const std::string name = tool["name"];
            if (name != "nav_bake" && name != "nav_path" && name != "nav_agents") continue;
            ++found;
            const bool readOnly = tool.contains("annotations") && tool["annotations"].value("readOnlyHint", false);
            CHECK(readOnly == (name != "nav_bake"));
        }
        CHECK(found == 3);
    }

    return TestResult();
}
