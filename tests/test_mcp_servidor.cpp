// Transporte HTTP do MCP sem a engine (motor_mcp, Hub residente): caminho configurável, token
// (Authorization: Bearer) e sessão (Mcp-Session-Id), como o MCP do Hub usa. O transporte básico (POST atendido
// na thread principal, origem de fora, respostas para depois) fica no test_mcp.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <httplib.h>  // antes de qualquer windows.h (winsock2)

#include "TestUtil.h"

#include <motor/mcp/HubLink.h>
#include <motor/mcp/McpProtocol.h>
#include <motor/mcp/McpServer.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>

using namespace motor::mcp;

namespace {

Json Request(int id, const char* method, Json params = Json::object()) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}};
}

// Roda `client` numa thread enquanto esta faz o papel da thread principal (Pump a cada "quadro").
void Serve(Server& server, const std::function<void()>& client) {
    std::atomic<bool> done = false;
    std::thread thread([&] {
        client();
        done = true;
    });
    for (int i = 0; i < 2000 && !done; ++i) {
        server.Pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    thread.join();
}

int StartFree(Server& server, int first) {
    std::string error;
    for (int port = first; port < first + 10; ++port) {
        if (server.Start(port, error)) return port;
    }
    return 0;
}

} // namespace

int main(int, char**) {
    Protocol protocol("teste-hub", "1.0");
    protocol.AddTool({.name = "eco", .description = "eco", .inputSchema = Schema(),
                      .run = [](const Json&) { return ToolResult::Text("pong"); }});

    SECTION("Caminho, token e sessão");
    {
        Server server(protocol);
        server.SetPath("/mcphub");
        server.SetToken("segredo-123");
        server.SetSessions(true);
        const int port = StartFree(server, 47610);
        CHECK(port > 0 && server.Url() == "http://127.0.0.1:" + std::to_string(port) + "/mcphub");

        int noToken = 0, wrongToken = 0, oldPath = 0, initStatus = 0, callStatus = 0, unknownSession = 0;
        int deleteStatus = 0, getStatus = 0, foreign = 0;
        std::string sessionA, sessionB, callBody;
        Serve(server, [&] {
            httplib::Client http("127.0.0.1", port);
            http.set_read_timeout(10, 0);
            const std::string init = Request(1, "initialize").dump();
            const httplib::Headers auth{{"Authorization", "Bearer segredo-123"}};
            if (auto r = http.Post("/mcphub", init, "application/json")) noToken = r->status;
            if (auto r = http.Post("/mcphub", {{"Authorization", "Bearer segredo-124"}}, init, "application/json")) {
                wrongToken = r->status;
            }
            if (auto r = http.Post("/mcp", auth, init, "application/json")) oldPath = r->status;
            if (auto r = http.Post("/mcphub", auth, init, "application/json")) {
                initStatus = r->status;
                sessionA = r->get_header_value("Mcp-Session-Id");
            }
            if (auto r = http.Post("/mcphub", auth, init, "application/json")) sessionB = r->get_header_value("Mcp-Session-Id");
            httplib::Headers withSession = auth;
            withSession.emplace("Mcp-Session-Id", sessionA);
            const Json call = Request(2, "tools/call", {{"name", "eco"}, {"arguments", Json::object()}});
            if (auto r = http.Post("/mcphub", withSession, call.dump(), "application/json")) {
                callStatus = r->status;
                callBody = r->body;
                // Só o initialize ganha uma sessão nova.
                if (r->has_header("Mcp-Session-Id")) callStatus = -1;
            }
            // Id de uma sessão que este servidor não deu (ele reiniciou): continua valendo.
            httplib::Headers stale = auth;
            stale.emplace("Mcp-Session-Id", "0123456789abcdef0123456789abcdef");
            if (auto r = http.Post("/mcphub", stale, Request(3, "ping").dump(), "application/json")) {
                unknownSession = r->status;
            }
            if (auto r = http.Delete("/mcphub", withSession)) deleteStatus = r->status;
            if (auto r = http.Get("/mcphub", auth)) getStatus = r->status;
            httplib::Headers outside = auth;
            outside.emplace("Origin", "https://site-qualquer.com");
            if (auto r = http.Post("/mcphub", outside, Request(4, "ping").dump(), "application/json")) foreign = r->status;
        });
        CHECK(noToken == 401 && wrongToken == 401);
        CHECK(oldPath == 404);
        CHECK(initStatus == 200 && sessionA.size() == 32 && !sessionB.empty() && sessionA != sessionB);
        CHECK(callStatus == 200 && Json::parse(callBody)["result"]["content"][0]["text"] == "pong");
        CHECK(unknownSession == 200);
        CHECK(deleteStatus == 200 && getStatus == 405);
        CHECK(foreign == 403);
        server.Stop();
    }

    SECTION("Sem token nem sessão (o editor): como antes");
    {
        Server server(protocol);
        const int port = StartFree(server, 47620);
        CHECK(port > 0 && server.Url() == "http://127.0.0.1:" + std::to_string(port) + "/mcp");
        int initStatus = 0, deleteStatus = 0;
        bool hasSession = true;
        Serve(server, [&] {
            httplib::Client http("127.0.0.1", port);
            http.set_read_timeout(10, 0);
            if (auto r = http.Post("/mcp", Request(1, "initialize").dump(), "application/json")) {
                initStatus = r->status;
                hasSession = r->has_header("Mcp-Session-Id");
            }
            if (auto r = http.Delete("/mcp")) deleteStatus = r->status;
        });
        CHECK(initStatus == 200 && !hasSession && deleteStatus == 405);
        server.Stop();
    }

    SECTION("Rota a mais (avisos dos editores ao Hub) e o cliente do HubLink");
    {
        Server server(protocol);
        server.SetPath("/mcphub");
        server.SetToken("segredo-do-hub");
        const std::thread::id mainThread = std::this_thread::get_id();
        bool onMainThread = false;
        std::string seenAuth, seenBody;
        server.AddRoute("/mcphub/editor", [&](const std::string& authorization, const std::string& body) {
            onMainThread = std::this_thread::get_id() == mainThread;
            seenAuth = authorization;
            seenBody = body;
            if (authorization != "Bearer do-editor") return Server::RouteReply{401, R"({"error": "quem?"})"};
            return Server::RouteReply{200, R"({"ok": true})"};
        });
        const int port = StartFree(server, 47620);
        CHECK(port > 0);

        bool sent = false, refused = false;
        std::string refusedError;
        int foreign = 0, mcpWithEditorToken = 0;
        Serve(server, [&] {
            // A rota tem o token dela: o do servidor não vale nem é pedido.
            sent = hublink::PostJson(port, "/mcphub/editor", "do-editor", {{"event", "pronto"}, {"port", 47801}});
            refused = !hublink::PostJson(port, "/mcphub/editor", "outro", {{"event", "pronto"}}, &refusedError);
            httplib::Client http("127.0.0.1", port);
            if (auto r = http.Post("/mcphub/editor", {{"Origin", "http://site.com"}}, "{}", "application/json")) {
                foreign = r->status;
            }
            // E o token da rota não abre o MCP.
            if (auto r = http.Post("/mcphub", {{"Authorization", "Bearer do-editor"}}, Request(1, "ping").dump(),
                                   "application/json")) {
                mcpWithEditorToken = r->status;
            }
        });
        CHECK(sent);
        CHECK(refused && refusedError.find("401") != std::string::npos);
        CHECK(foreign == 403);
        CHECK(mcpWithEditorToken == 401);
        CHECK(onMainThread);  // atendida no Pump, como as mensagens
        CHECK(Json::parse(seenBody, nullptr, false).value("event", "") == "pronto");

        CHECK(!hublink::PortFree(port));  // ocupada pelo servidor
        server.Stop();
        CHECK(hublink::PortFree(port));
        CHECK(!hublink::PostJson(port, "/mcphub/editor", "do-editor", Json::object(), nullptr, 300));  // fechado
        CHECK(hublink::SameSecret("abc", "abc") && !hublink::SameSecret("abc", "abd") &&
              !hublink::SameSecret("abc", "abcd"));
    }

    return TestResult();
}
