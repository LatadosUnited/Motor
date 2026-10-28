// Servidor MCP do editor (Fase 13): protocolo JSON-RPC, ferramentas de cena, imagem (PNG/base64) e o
// transporte HTTP de verdade (um cliente numa thread, a "thread principal" do teste atendendo com Pump).
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif
#include <httplib.h>

#include "TestUtil.h"

#include <EditorContext.h>
#include <mcp/ImageEncode.h>
#include <mcp/Mcp.h>
#include <mcp/McpSceneTools.h>

#include <motor/scene/SceneRuntime.h>

#include <atomic>
#include <cmath>
#include <optional>
#include <string>
#include <thread>

using namespace editor;
using namespace editor::mcp;

namespace {
Json Request(int id, const char* method, Json params = Json::object()) {
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}};
}

// tools/call -> {isError, texto do primeiro conteúdo}
Json Call(Protocol& protocol, const char* tool, Json args) {
    static int id = 100;
    const auto response = protocol.Handle(Request(++id, "tools/call", {{"name", tool}, {"arguments", std::move(args)}}));
    if (!response || !response->contains("result")) return Json{{"rpc_error", response ? (*response)["error"] : Json()}};
    const Json& result = (*response)["result"];
    Json out = {{"isError", result["isError"]}};
    if (!result["content"].empty() && result["content"][0]["type"] == "text") {
        out["text"] = result["content"][0]["text"];
        out["data"] = Json::parse(result["content"][0]["text"].get<std::string>(), nullptr, false);
    }
    return out;
}
} // namespace

int main() {
    SECTION("Protocolo: initialize, ping, tools/list, erros e notificações");
    {
        Protocol protocol("teste", "1.0", "instruções");
        protocol.AddTool({.name = "eco",
                          .description = "devolve o texto",
                          .inputSchema = Schema({{"texto", Prop("string", "o texto")}}, {"texto"}),
                          .readOnly = true,
                          .run = [](const Json& args) {
                              if (!args.contains("texto")) throw ToolError("falta texto");
                              return ToolResult::Text(args["texto"].get<std::string>());
                          }});

        auto init = protocol.Handle(Request(1, "initialize", {{"protocolVersion", "2025-03-26"}, {"capabilities", Json::object()}}));
        CHECK(init && (*init)["id"] == 1 && (*init)["result"]["protocolVersion"] == "2025-03-26");
        CHECK(init && (*init)["result"]["serverInfo"]["name"] == "teste" && (*init)["result"]["instructions"] == "instruções");
        CHECK(init && (*init)["result"]["capabilities"].contains("tools"));
        auto unknownVersion = protocol.Handle(Request(2, "initialize", {{"protocolVersion", "1999-01-01"}}));
        CHECK(unknownVersion && (*unknownVersion)["result"]["protocolVersion"] == Protocol::kLatestVersion);

        CHECK(!protocol.Handle(Json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}));
        auto ping = protocol.Handle(Request(3, "ping"));
        CHECK(ping && (*ping)["result"].is_object());

        auto list = protocol.Handle(Request(4, "tools/list"));
        CHECK(list && (*list)["result"]["tools"].size() == 1 && (*list)["result"]["tools"][0]["name"] == "eco");
        CHECK(list && (*list)["result"]["tools"][0]["annotations"]["readOnlyHint"] == true);

        auto missing = protocol.Handle(Request(5, "recursos/inventados"));
        CHECK(missing && (*missing)["error"]["code"] == -32601);
        CHECK(Call(protocol, "nao_existe", Json::object())["rpc_error"]["code"] == -32602);
        CHECK(Call(protocol, "eco", {{"texto", "olá"}})["text"] == "olá");
        // Obrigatório faltando: o protocolo recusa antes de chamar a ferramenta (que leria args["texto"] direto).
        const Json bad = Call(protocol, "eco", Json::object());
        CHECK(bad["isError"] == true && bad["text"] == "falta o argumento obrigatório 'texto'");
        protocol.AddTool({.name = "direto",
                          .description = "lê o argumento sem conferir",
                          .inputSchema = Schema({{"pasta", Prop("string", "a pasta")}}, {"pasta"}),
                          .run = [](const Json& args) { return ToolResult::Text(args["pasta"].get<std::string>()); }});
        CHECK(Call(protocol, "direto", {{"local", "C:/x"}})["isError"] == true);  // antes: assert e o editor caía
        CHECK(Call(protocol, "direto", {{"pasta", 3}})["isError"] == true);       // tipo errado: erro, não queda
        CHECK(Call(protocol, "direto", {{"pasta", "C:/x"}})["text"] == "C:/x");

        CHECK(Json::parse(protocol.HandleText("{nada"))["error"]["code"] == -32700);
        CHECK(protocol.HandleText(R"({"jsonrpc":"2.0","method":"notifications/cancelled"})").empty());
        const Json batch = Json::parse(protocol.HandleText(R"([{"jsonrpc":"2.0","id":7,"method":"ping"}])"));
        CHECK(batch.is_array() && batch.size() == 1 && batch[0]["id"] == 7);
    }

    SECTION("Ferramentas de cena: criar, mudar, componentes, campos, apagar");
    {
        EditorContext ctx;
        Protocol protocol("teste", "1.0");
        AddSceneTools(protocol, ctx);

        const Json torre = Call(protocol, "entity_create", {{"kind", "empty"}, {"name", "Torre"}, {"position", {1, 2, 3}}});
        CHECK(torre["isError"] == false && torre["data"]["name"] == "Torre");
        const int torreId = torre["data"]["id"].get<int>();
        CHECK(ctx.selected == static_cast<motor::EntityId>(torreId));
        CHECK(Call(protocol, "select", Json::object())["isError"] == true);  // sem 'entity': erro, seleção mantida
        CHECK(ctx.selected == static_cast<motor::EntityId>(torreId));
        CHECK(torre["data"]["transform"]["position"] == Json::array({1.0, 2.0, 3.0}));

        const Json base = Call(protocol, "entity_create", {{"kind", "empty"}, {"name", "Base"}, {"parent", "Torre"}});
        const int baseId = base["data"]["id"].get<int>();

        // Seleção múltipla (Fase 20): várias de uma vez, somar uma, e a lista devolvida.
        const Json both = Call(protocol, "select", {{"entities", {"Torre", baseId}}});
        CHECK(both["isError"] == false && both["data"]["selection"].size() == 2);
        CHECK(ctx.selected == static_cast<motor::EntityId>(baseId) && ctx.SelectionCount() == 2);
        Call(protocol, "select", {{"entity", "Torre"}});
        CHECK(ctx.SelectionCount() == 1);
        Call(protocol, "select", {{"entity", baseId}, {"add", true}});
        CHECK(ctx.SelectionCount() == 2 && ctx.IsSelected(static_cast<motor::EntityId>(torreId)));
        CHECK(Call(protocol, "select", {{"entities", {"NaoExiste"}}})["isError"] == true);
        CHECK(Call(protocol, "scene_tree", Json::object())["data"]["selection"].size() == 2);

        // Busca como a da Hierarquia (Fase 20).
        const Json search = Call(protocol, "scene_search", {{"query", "base"}, {"select", true}});
        CHECK(search["isError"] == false && search["data"]["count"] == 1);
        CHECK(search["data"]["entities"][0]["path"] == "Torre");
        CHECK(ctx.selected == static_cast<motor::EntityId>(baseId) && ctx.SelectionCount() == 1);
        CHECK(Call(protocol, "scene_search", {{"query", " "}})["isError"] == true);

        // Copiar, recortar e colar (Fase 20).
        CHECK(Call(protocol, "entity_paste", Json::object())["isError"] == true);  // nada copiado
        CHECK(Call(protocol, "entity_copy", {{"entities", {"Torre"}}})["isError"] == false);
        const size_t count = ctx.scene.Entities().size();
        const Json pasted = Call(protocol, "entity_paste", Json::object());
        CHECK(pasted["isError"] == false && pasted["data"]["pasted"].size() == 1);
        CHECK(ctx.scene.Entities().size() == count + 2);  // Torre com a Base
        const int copyId = pasted["data"]["pasted"][0]["id"].get<int>();
        CHECK(Call(protocol, "entity_cut", {{"entities", {copyId}}})["isError"] == false);
        CHECK(ctx.scene.Entities().size() == count && !ctx.scene.Find(static_cast<motor::EntityId>(copyId)));
        Call(protocol, "select", {{"entity", baseId}});
        CHECK(base["data"]["parent"] == torreId);

        const Json tree = Call(protocol, "scene_tree", Json::object());
        CHECK(tree["data"]["entity_count"] == 2 && tree["data"]["entities"][0]["children"][0]["name"] == "Base");

        CHECK(!ctx.TakeForgottenAngles(baseId));
        const Json moved = Call(protocol, "entity_update", {{"entity", baseId}, {"rotation", {0, 30, 0}}, {"name", "Pé"}});
        CHECK(moved["data"]["name"] == "Pé");
        CHECK(std::abs(moved["data"]["transform"]["rotation"][1].get<float>() - 30.0f) < 0.01f);
        // Rotação mudada fora do Inspector: ele recalcula os ângulos dessa entidade (uma vez), não os das outras.
        CHECK(ctx.TakeForgottenAngles(baseId));
        CHECK(!ctx.TakeForgottenAngles(baseId));
        Call(protocol, "entity_update", {{"entity", baseId}, {"rotation", {0, 45, 0}}});
        CHECK(!ctx.TakeForgottenAngles(torreId));
        ctx.ForgetAngles();  // cena trocada: todos
        CHECK(ctx.TakeForgottenAngles(torreId));
        CHECK(Call(protocol, "entity_update", {{"entity", torreId}, {"parent", baseId}})["isError"] == true);  // ciclo

        const Json added = Call(protocol, "component_add",
                                {{"entity", "Pé"}, {"type", "mesh renderer"}, {"values", {{"color", {1, 0, 0, 1}}}}});
        CHECK(added["isError"] == false);
        CHECK(added["data"]["components"][0]["type"] == "MeshRenderer");
        CHECK(added["data"]["components"][0]["fields"]["color"] == Json::array({1.0, 0.0, 0.0, 1.0}));
        const motor::MeshRenderer* renderer = ctx.scene.Find(baseId)->Get<motor::MeshRenderer>();
        CHECK(renderer && renderer->material.color.r == 1.0f && renderer->material.color.g == 0.0f);

        CHECK(Call(protocol, "component_set", {{"entity", baseId}, {"type", "MeshRenderer"}, {"values", {{"roughness", 0.25}}}})["isError"] == false);
        CHECK(renderer && renderer->material.roughness == 0.25f);
        const Json wrongField = Call(protocol, "component_set", {{"entity", baseId}, {"type", "MeshRenderer"}, {"values", {{"cor_errada", 1}}}});
        CHECK(wrongField["isError"] == true && wrongField["text"].get<std::string>().find("color") != std::string::npos);

        // Valores de tipo errado respondem com erro e o formato esperado, sem alterar o componente.
        const Json badFloat = Call(protocol, "component_set",
                                   {{"entity", baseId}, {"type", "MeshRenderer"}, {"values", {{"roughness", "texto"}}}});
        CHECK(badFloat["isError"] == true && badFloat["text"].get<std::string>().find("float") != std::string::npos);
        CHECK(renderer && renderer->material.roughness == 0.25f);

        const Json badVec4 = Call(protocol, "component_set",
                                  {{"entity", baseId}, {"type", "MeshRenderer"}, {"values", {{"roughness", 0.9}, {"color", {1, 2}}}}});
        CHECK(badVec4["isError"] == true && badVec4["text"].get<std::string>().find("vec4") != std::string::npos);
        CHECK(renderer && renderer->material.roughness == 0.25f);  // atômico: nem o primeiro campo mudou

        // Enum inválido em component_add: responde com os valores aceitos e não deixa o componente adicionado.
        const Json badEnum = Call(protocol, "component_add",
                                  {{"entity", baseId}, {"type", "Collider"}, {"values", {{"shape", "Triangulo"}}}});
        CHECK(badEnum["isError"] == true && badEnum["text"].get<std::string>().find("Box") != std::string::npos);
        CHECK(!ctx.scene.Find(baseId)->Has<motor::ColliderComponent>());

        CHECK(Call(protocol, "component_add", {{"entity", baseId}, {"type", "NaoExiste"}})["isError"] == true);
        CHECK(Call(protocol, "entity_get", {{"entity", "Ninguém"}})["isError"] == true);

        const Json types = Call(protocol, "component_types", Json::object());
        bool hasMesh = false;
        for (const Json& t : types["data"]["types"]) hasMesh |= t["name"] == "MeshRenderer";
        CHECK(hasMesh);

        CHECK(Call(protocol, "component_remove", {{"entity", baseId}, {"type", "MeshRenderer"}})["isError"] == false);
        CHECK(!ctx.scene.Find(baseId)->Has<motor::MeshRenderer>());

        const Json env = Call(protocol, "scene_environment", {{"fog_density", 0.0}, {"sky_color", {0, 0, 0}}});
        CHECK(env["data"]["fog_density"] == 0.0 && ctx.scene.environment.skyColor == glm::vec3(0.0f));

        const Json copy = Call(protocol, "entity_duplicate", {{"entity", torreId}});
        CHECK(copy["isError"] == false && ctx.scene.Entities().size() == 4);
        CHECK(Call(protocol, "entity_delete", {{"entity", torreId}})["isError"] == false);
        CHECK(ctx.scene.Entities().size() == 2 && ctx.scene.Find(torreId) == nullptr);
    }

    SECTION("Retorno de uso (19/10/2026): rotação, collider, estado do jogo, sombra");
    {
        EditorContext ctx;
        motor::SceneRuntime* running = nullptr;
        Protocol protocol("teste", "1.0");
        AddSceneTools(protocol, ctx, [&running] { return running; });

        // Rotação devolvida como foi dada: [0, 180, 0], não [180, 0, 180] (o mesmo giro).
        const Json virado = Call(protocol, "entity_create", {{"kind", "empty"}, {"name", "Virado"}, {"rotation", {0, 180, 0}}});
        CHECK(virado["data"]["transform"]["rotation"] == Json::array({0.0, 180.0, 0.0}));
        const Json torto = Call(protocol, "entity_update", {{"entity", "Virado"}, {"rotation", {10, 135, -20}}});
        const Json r = torto["data"]["transform"]["rotation"];
        CHECK(std::abs(r[0].get<float>() - 10.0f) < 0.01f && std::abs(r[1].get<float>() - 135.0f) < 0.01f &&
              std::abs(r[2].get<float>() + 20.0f) < 0.01f);
        // Qualquer rotação: os ângulos devolvidos refazem a mesma rotação.
        bool same = true;
        for (int i = 0; i < 200; ++i) {
            motor::Transform t;
            t.SetEulerDegrees({float((i * 37) % 360 - 180), float((i * 53) % 180 - 90), float((i * 71) % 360 - 180)});
            motor::Transform back;
            back.SetEulerDegrees(t.EulerDegrees());
            same &= std::abs(glm::dot(t.rotation, back.rotation)) > 0.9999f;
        }
        CHECK(same);

        // Formas criadas pelo MCP vêm com o Collider, como no menu Entidade.
        const Json cubo = Call(protocol, "entity_create", {{"kind", "cube"}, {"name", "Chão"}, {"scale", {20, 1, 20}}});
        bool hasCollider = false;
        for (const Json& c : cubo["data"]["components"]) hasCollider |= c["type"] == "Collider";
        CHECK(hasCollider);
        ctx.scene.FindByName("Chão")->transform.position = {0.0f, -0.5f, 0.0f};

        // Sombra do Mesh Renderer: "só sombra" pelo campo da cena.
        CHECK(Call(protocol, "component_set", {{"entity", "Chão"}, {"type", "MeshRenderer"},
                                               {"values", {{"shadows", "ShadowsOnly"}}}})["isError"] == false);
        CHECK(ctx.scene.FindByName("Chão")->Get<motor::MeshRenderer>()->shadows == motor::ShadowCasting::ShadowsOnly);
        CHECK(Call(protocol, "component_set", {{"entity", "Chão"}, {"type", "MeshRenderer"},
                                               {"values", {{"shadows", "Talvez"}}}})["isError"] == true);

        // Estado do jogo no entity_get: só no Play; no chão e velocidade do personagem.
        Call(protocol, "entity_create", {{"kind", "empty"}, {"name", "Herói"}, {"position", {0, 1.0, 0}}});
        Call(protocol, "component_add", {{"entity", "Herói"}, {"type", "CharacterController"}});
        CHECK(!Call(protocol, "entity_get", {{"entity", "Herói"}})["data"].contains("runtime"));
        {
            motor::SceneRuntime runtime(ctx.scene);
            runtime.Start();
            for (int i = 0; i < 60; ++i) {
                runtime.FixedUpdate(1.0f / 60.0f);
                runtime.Update(1.0f / 60.0f);
            }
            running = &runtime;
            const Json heroi = Call(protocol, "entity_get", {{"entity", "Herói"}})["data"];
            CHECK(heroi.contains("runtime") && heroi["runtime"]["frame"] == 60);
            CHECK(heroi["runtime"]["grounded"] == true);
            CHECK(heroi["runtime"]["velocity"].size() == 3 && heroi["runtime"].contains("horizontal_speed"));
            CHECK(!heroi["runtime"].contains("animator"));  // sem Animator
            running = nullptr;
            runtime.Stop();
        }
    }

    SECTION("Resposta para depois (wait): o servidor confere a cada quadro");
    {
        Protocol protocol("teste", "1.0");
        bool ready = false;
        int checks = 0;
        protocol.AddTool({.name = "espera", .description = "responde quando ready", .inputSchema = Schema(),
                          .run = [&](const Json&) {
                              ToolResult now = ToolResult::Text("pedido");
                              now.later = [&](ToolResult& out) {
                                  ++checks;
                                  if (!ready) return false;
                                  out = ToolResult::Text("pronto");
                                  return true;
                              };
                              return now;
                          }});
        protocol.AddTool({.name = "falha", .description = "erro na espera", .inputSchema = Schema(),
                          .run = [](const Json&) {
                              ToolResult now = ToolResult::Text("pedido");
                              now.later = [](ToolResult&) -> bool { throw ToolError("não valeu a tempo"); };
                              return now;
                          }});
        // Sem quem espere (testes, Handle direto): vale a resposta de agora.
        CHECK(Call(protocol, "espera", Json::object())["text"] == "pedido");
        const std::string body = Request(9, "tools/call", {{"name", "espera"}, {"arguments", Json::object()}}).dump();
        std::optional<Deferred> deferred;
        CHECK(protocol.HandleText(body, &deferred).empty() && deferred.has_value());
        CHECK(!deferred->Poll().has_value());
        // Um ping depois não herda a espera da chamada anterior.
        std::optional<Deferred> none;
        CHECK(!protocol.HandleText(Request(10, "ping").dump(), &none).empty() && !none);
        ready = true;
        const auto reply = deferred->Poll();
        CHECK(reply && Json::parse(*reply)["id"] == 9 &&
              Json::parse(*reply)["result"]["content"][0]["text"] == "pronto");
        std::optional<Deferred> failing;
        protocol.HandleText(Request(11, "tools/call", {{"name", "falha"}, {"arguments", Json::object()}}).dump(), &failing);
        const auto failed = failing ? failing->Poll() : std::nullopt;
        CHECK(failed && Json::parse(*failed)["result"]["isError"] == true);

        // Pelo HTTP: a resposta só sai depois de alguns quadros, e outros pedidos são atendidos no meio.
        ready = false;
        checks = 0;
        Server server(protocol);
        std::string error;
        int port = 0;
        for (int candidate = 47880; candidate < 47890 && !server.IsRunning(); ++candidate) {
            if (server.Start(candidate, error)) port = candidate;
        }
        CHECK(server.IsRunning());
        std::atomic<bool> waited = false;
        std::string waitBody;
        std::thread slow([&] {
            httplib::Client http("127.0.0.1", port);
            http.set_read_timeout(10, 0);
            if (auto r = http.Post("/mcp", body, "application/json")) waitBody = r->body;
            waited = true;
        });
        std::atomic<int> pingStatus = 0;
        std::thread pinger;
        for (int i = 0; i < 2000 && !waited; ++i) {
            server.Pump();
            if (i == 40) {  // ainda esperando: um ping de outro cliente é respondido normalmente
                pinger = std::thread([&] {
                    httplib::Client http("127.0.0.1", port);
                    http.set_read_timeout(10, 0);
                    if (auto r = http.Post("/mcp", Request(12, "ping").dump(), "application/json")) pingStatus = r->status;
                });
            }
            if (i == 80) ready = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        slow.join();
        for (int i = 0; i < 400 && pingStatus == 0; ++i) {
            server.Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (pinger.joinable()) pinger.join();
        CHECK(waited && Json::parse(waitBody)["result"]["content"][0]["text"] == "pronto");
        CHECK(checks > 10);  // conferido a cada quadro, não de uma vez
        CHECK(pingStatus == 200);
        server.Stop();
    }

    SECTION("Imagem: base64, PNG e redução");
    {
        CHECK(Base64({'M', 'a', 'n'}) == "TWFu");
        CHECK(Base64({'M', 'a'}) == "TWE=");
        CHECK(Base64({'M'}) == "TQ==");
        Image image{4, 2, std::vector<uint8_t>(4 * 2 * 4, 200)};
        const std::vector<uint8_t> png = EncodePng(image);
        CHECK(png.size() > 8 && png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
        const Image small = Downscale(Image{100, 50, std::vector<uint8_t>(100 * 50 * 4, 10)}, 40, 40);
        CHECK(small.width == 40 && small.height == 20 && small.rgba.size() == 40 * 20 * 4 && small.rgba[0] == 10);
        CHECK(Downscale(image, 100, 100).width == 4);  // não aumenta

        // Ida e volta pelo leitor de imagens da engine (stb_image): o deflate e os filtros próprios precisam
        // gerar um PNG que qualquer leitor entende, com os mesmos pixels. Imagem com áreas lisas (repetição
        // longa), degradê e ruído (sem repetição), maior que a janela de 32 KB do deflate.
        Image scene{300, 200, std::vector<uint8_t>(300 * 200 * 4)};
        uint32_t noise = 12345;
        for (int y = 0; y < scene.height; ++y) {
            for (int x = 0; x < scene.width; ++x) {
                uint8_t* p = &scene.rgba[(size_t(y) * scene.width + x) * 4];
                noise = noise * 1664525u + 1013904223u;
                if (y < 80) {
                    p[0] = 135; p[1] = 179; p[2] = 224;  // céu liso
                } else if (x < 150) {
                    p[0] = uint8_t(x); p[1] = uint8_t(y); p[2] = uint8_t(x + y);  // degradê
                } else {
                    p[0] = uint8_t(noise >> 24); p[1] = uint8_t(noise >> 16); p[2] = uint8_t(noise >> 8);
                }
                p[3] = 255;
            }
        }
        const std::vector<uint8_t> encoded = EncodePng(scene);
        motor::ImageData decoded;
        std::string decodeError;
        CHECK(motor::DecodeImage(encoded, decoded, &decodeError));
        CHECK(decoded.width == 300 && decoded.height == 200);
        bool same = decoded.pixels.size() == scene.rgba.size();
        for (int y = 0; same && y < scene.height; ++y) {
            // DecodeImage entrega a linha 0 embaixo (convenção do OpenGL).
            const uint8_t* got = &decoded.pixels[size_t(scene.height - 1 - y) * scene.width * 4];
            const uint8_t* want = &scene.rgba[size_t(y) * scene.width * 4];
            for (int i = 0; i < scene.width * 4; ++i) same &= got[i] == want[i];
        }
        CHECK(same);
        CHECK(encoded.size() < scene.rgba.size() * 3 / 4 * 3 / 4);  // comprimiu (o ruído não comprime)
        std::printf("    PNG 300x200: %zu bytes (RGB cru: %zu)\n", encoded.size(), size_t(300 * 200 * 3));
    }

    SECTION("Servidor HTTP: POST /mcp atendido na thread principal; origem de fora recusada");
    {
        Protocol protocol("teste-http", "1.0");
        protocol.AddTool({.name = "eco", .description = "eco", .inputSchema = Schema(),
                          .run = [](const Json&) { return ToolResult::Text("pong"); }});
        Server server(protocol);
        std::string error;
        int port = 0;
        for (int candidate = 47890; candidate < 47900 && !server.IsRunning(); ++candidate) {
            if (server.Start(candidate, error)) port = candidate;
        }
        CHECK(server.IsRunning() && port > 0);

        std::atomic<bool> done = false;
        int initStatus = 0, callStatus = 0, notifyStatus = 0, foreignStatus = 0, getStatus = 0;
        std::string initBody, callBody;
        std::thread client([&] {
            httplib::Client http("127.0.0.1", port);
            http.set_read_timeout(10, 0);
            if (auto r = http.Post("/mcp", Request(1, "initialize").dump(), "application/json")) {
                initStatus = r->status;
                initBody = r->body;
            }
            const Json call = Request(2, "tools/call", {{"name", "eco"}, {"arguments", Json::object()}});
            if (auto r = http.Post("/mcp", call.dump(), "application/json")) {
                callStatus = r->status;
                callBody = r->body;
            }
            if (auto r = http.Post("/mcp", R"({"jsonrpc":"2.0","method":"notifications/initialized"})", "application/json")) {
                notifyStatus = r->status;
            }
            if (auto r = http.Post("/mcp", httplib::Headers{{"Origin", "https://site-qualquer.com"}},
                                   Request(3, "ping").dump(), "application/json")) {
                foreignStatus = r->status;
            }
            if (auto r = http.Get("/mcp")) getStatus = r->status;
            done = true;
        });
        // "Thread principal": atende os pedidos como o editor faz a cada quadro.
        for (int i = 0; i < 2000 && !done; ++i) {
            server.Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        client.join();
        CHECK(initStatus == 200 && Json::parse(initBody)["result"]["serverInfo"]["name"] == "teste-http");
        CHECK(callStatus == 200 && Json::parse(callBody)["result"]["content"][0]["text"] == "pong");
        CHECK(notifyStatus == 202);
        CHECK(foreignStatus == 403);
        CHECK(getStatus == 405);
        server.Stop();
        CHECK(!server.IsRunning());
    }

    return TestResult();
}
