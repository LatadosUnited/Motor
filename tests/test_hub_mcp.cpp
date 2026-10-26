// Ferramentas do /mcphub (Hub residente): o HubApp de verdade, sem janela nem HTTP, chamado pelo Protocol. O editor
// da versão instalada é uma cópia deste teste: com --project ou --create ele só espera (até ser fechado).
#include "TestUtil.h"

#include <HubApp.h>
#include <HubMcp.h>

#include <motor/mcp/HubLink.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

namespace fs = std::filesystem;
namespace link = motor::mcp::hublink;
using motor::mcp::Json;

namespace {

struct Call {
    bool ok = false;
    std::string text;
    Json data;  // o texto lido como JSON, quando é (ToolResult::Data)
};

Call Read(const Json& response) {
    Call c;
    const Json& result = response["result"];
    c.ok = !result.value("isError", false);
    c.text = result["content"][0].value("text", std::string());
    c.data = Json::parse(c.text, nullptr, false);
    return c;
}

Json Request(const char* tool, Json args) {
    return {{"jsonrpc", "2.0"},
            {"id", 1},
            {"method", "tools/call"},
            {"params", {{"name", tool}, {"arguments", std::move(args)}}}};
}

// Resposta imediata (sem esperar o `later`).
Call Now(hub::HubMcp& mcp, const char* tool, Json args = Json::object()) {
    return Read(*mcp.GetProtocol().Handle(Request(tool, std::move(args))));
}

// Como o servidor HTTP: espera o `later` (no máximo `seconds`).
Call Wait(hub::HubMcp& mcp, const char* tool, Json args, double seconds = 20.0) {
    std::optional<motor::mcp::Deferred> deferred;
    const std::string text = mcp.GetProtocol().HandleText(Request(tool, std::move(args)).dump(), &deferred);
    if (!deferred) return Read(Json::parse(text));
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(seconds * 1000));
    while (std::chrono::steady_clock::now() < end) {
        mcp.Pump();  // os avisos dos editores (rota do servidor) chegam por aqui
        if (auto done = deferred->Poll()) return Read(Json::parse(*done));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return {false, "o teste cansou de esperar", nullptr};
}

void WriteText(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
}

std::string Environment(const char* name) {
#ifdef _WIN32
    const std::wstring wide(name, name + std::strlen(name));
    const wchar_t* value = _wgetenv(wide.c_str());
    return value ? hub::ToUtf8(fs::path(value)) : std::string();
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

void SetEnvironment(const char* name, const std::string& value) {
#ifdef _WIN32
    const std::wstring wide(name, name + std::strlen(name));
    _wputenv_s(wide.c_str(), hub::PathFromUtf8(value).wstring().c_str());  // vazio apaga
#else
    setenv(name, value.c_str(), 1);
#endif
}

} // namespace

int main(int argc, char** argv) {
    // Papel de editor falso: o hub abriu esta cópia com --project/--create. Com --hub, avisa o hub como o editor
    // (HubLink.h); depois espera ser fechado (no máximo 60 s).
    bool fake = false;
    int mcpPort = 0, hubPort = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--project") == 0 || std::strcmp(argv[i], "--create") == 0) fake = true;
        if (std::strncmp(argv[i], "--mcp=", 6) == 0) mcpPort = std::atoi(argv[i] + 6);
        if (std::strcmp(argv[i], link::kArgument) == 0 && i + 1 < argc) hubPort = std::atoi(argv[i + 1]);
    }
    if (fake) {
        if (hubPort > 0) {
            const std::string token = Environment(link::kTokenEnv);
            // Projeto vazio: o argv do MinGW vem em ANSI e estragaria "Ação" (o hub ignora o vazio).
            link::PostJson(hubPort, link::kNotifyPath, token, {{"event", link::kEventReady}, {"port", mcpPort}});
            const std::string other = Environment("MOTOR_TESTE_OUTRO_PROJETO");
            if (!other.empty()) {
                link::PostJson(hubPort, link::kNotifyPath, token,
                               {{"event", link::kEventProject}, {"port", mcpPort}, {"project", other}});
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(60));
        return 0;
    }

    const fs::path dir = TestDir(argc, argv, "test_hub_mcp_tmp");
    const fs::path dataDir = dir / hub::PathFromUtf8("Dados do Hub");
    const fs::path versionDir = dataDir / hub::PathFromUtf8("Versões") / "0.9.0";
    fs::create_directories(versionDir);
    fs::copy_file(fs::absolute(argv[0]).replace_extension(".exe"), versionDir / "editor-falso.exe");
    WriteText(versionDir / "motor-version.json",
              R"({"format": "motor-version", "version": "0.9.0", "scriptApiVersion": 7, "buildType": "Debug",
                  "editor": "editor-falso.exe"})");
    const fs::path project = dir / hub::PathFromUtf8("Jogo Ação");
    fs::create_directories(project);
    WriteText(project / "Jogo.mproject",
              R"({"format": "motor-project", "name": "Jogo Ação", "engineVersion": "0.9.0"})");
    const std::string projectUtf8 = hub::ToUtf8(project);

    hub::HubApp app(hub::platform::DefaultPaths(dataDir), {}, dataDir);
    hub::HubMcp mcp(app);

    SECTION("lista de ferramentas");
    {
        const Json list = *mcp.GetProtocol().Handle({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}});
        std::vector<std::string> names;
        for (const Json& t : list["result"]["tools"]) names.push_back(t["name"]);
        for (const char* wanted : {"hub_status", "projects_list", "project_open", "project_create", "project_add",
                                   "project_remove", "editor_close", "versions_list", "version_set_default",
                                   "version_remove", "versions_available", "version_download"}) {
            CHECK(std::find(names.begin(), names.end(), wanted) != names.end());
        }
    }

    SECTION("hub_status e versions_list");
    {
        const Call status = Now(mcp, "hub_status");
        CHECK(status.ok);
        CHECK(status.data["versions"] == Json::array({"0.9.0"}));
        CHECK(status.data["defaultVersion"] == "0.9.0");
        CHECK(status.data["projects"] == 0);
        CHECK(status.data["editors"].empty());
        CHECK(status.data["download"].is_null());

        const Call versions = Now(mcp, "versions_list");
        CHECK(versions.ok && versions.data.size() == 1);
        CHECK(versions.data[0]["version"] == "0.9.0");
        CHECK(versions.data[0]["scriptApiVersion"] == 7);
        CHECK(versions.data[0]["default"] == true);
        CHECK(versions.data[0]["editorTools"].is_null());  // sem mcp-ferramentas.json (como as versões antigas)
        WriteText(versionDir / hub::kMcpToolsFile, R"({"format": "motor-mcp-tools", "version": 1, "tools": [{}, {}]})");
        CHECK(Now(mcp, "versions_list").data[0]["editorTools"] == 2);

        CHECK(Now(mcp, "version_set_default", {{"version", "0.9.0"}}).ok);
        CHECK(!Now(mcp, "version_set_default", {{"version", "5.0.0"}}).ok);
        CHECK(!Now(mcp, "version_set_default").ok);  // falta o argumento
    }

    SECTION("project_add, projects_list e project_remove");
    {
        CHECK(Now(mcp, "project_add", {{"path", projectUtf8}}).ok);
        CHECK(!Now(mcp, "project_add", {{"path", projectUtf8}}).ok);  // já na lista
        CHECK(!Now(mcp, "project_add", {{"path", hub::ToUtf8(dir / "nada")}}).ok);

        const Call list = Now(mcp, "projects_list");
        CHECK(list.ok && list.data.size() == 1);
        CHECK(list.data[0]["name"] == "Jogo Ação");
        CHECK(list.data[0]["engineVersion"] == "0.9.0");
        CHECK(list.data[0]["found"] == true);
        CHECK(list.data[0]["editorOpen"] == false);
        CHECK(Now(mcp, "projects_list", {{"query", "ação"}}).data.size() == 1);
        CHECK(Now(mcp, "projects_list", {{"query", "outro"}}).data.empty());

        CHECK(Now(mcp, "project_remove", {{"path", projectUtf8}}).ok);
        CHECK(Now(mcp, "projects_list").data.empty());
        CHECK(fs::exists(project / "Jogo.mproject"));  // só sai da lista
    }

    SECTION("project_open e editor_close");
    {
        const Call open = Now(mcp, "project_open", {{"path", projectUtf8}});
        CHECK(open.ok);
        CHECK(open.data.value("pid", 0) > 0);
        CHECK(!Now(mcp, "project_open", {{"path", projectUtf8}}).ok);  // já aberto
        CHECK(!Now(mcp, "project_open", {{"path", projectUtf8}, {"engine", "5.0.0"}}).ok);

        const Call list = Now(mcp, "projects_list");
        CHECK(list.data.size() == 1);  // abrir põe na lista
        CHECK(list.data[0]["editorOpen"] == true);
        CHECK(list.data[0]["editorPid"] == open.data["pid"]);
        const Call status = Now(mcp, "hub_status");
        CHECK(status.data["editors"].size() == 1);
        CHECK(status.data["editors"][0]["version"] == "0.9.0");

        // Com um editor da versão aberto, ela não vai para a Lixeira.
        const Call remove = Now(mcp, "version_remove", {{"version", "0.9.0"}});
        CHECK(!remove.ok);
        CHECK(fs::exists(versionDir / "motor-version.json"));

        // O editor falso não tem janela: o pedido de fechar sem force não tem a quem ir.
        CHECK(!Now(mcp, "editor_close", {{"path", projectUtf8}}).ok);
        const Call closed = Wait(mcp, "editor_close", {{"path", projectUtf8}, {"force", true}});
        CHECK(closed.ok);
        CHECK(closed.text.find("fechado") != std::string::npos);
        CHECK(Now(mcp, "projects_list").data[0]["editorOpen"] == false);
        CHECK(!Now(mcp, "editor_close", {{"path", projectUtf8}}).ok);  // nada aberto
    }

    SECTION("project_create");
    {
        CHECK(!Now(mcp, "project_create").ok);  // falta o nome
        CHECK(!Now(mcp, "project_create", {{"name", "Outro"}, {"engine", "5.0.0"}}).ok);
        CHECK(!Now(mcp, "project_create", {{"name", "Jogo Ação"}, {"parent", hub::ToUtf8(dir)}}).ok);  // já existe
        const Call created = Now(mcp, "project_create", {{"name", "Novo Jogo"}, {"parent", hub::ToUtf8(dir)}});
        CHECK(created.ok);
        CHECK(created.data["path"] == hub::ToUtf8((dir / "Novo Jogo").lexically_normal()));
        CHECK(Now(mcp, "hub_status").data["editors"].size() == 1);
        CHECK(Wait(mcp, "editor_close", {{"path", hub::ToUtf8(dir / "Novo Jogo")}, {"force", true}}).ok);
    }

    SECTION("servidores fora do ar");
    {
        // Porta 1 em loopback: recusa na hora. O erro vem por servidor, sem travar.
        const Call available = Wait(mcp, "versions_available", {{"server", "http://127.0.0.1:1"}});
        CHECK(available.ok);
        CHECK(available.data.size() == 1 && available.data[0].contains("error"));
        const Call download = Wait(mcp, "version_download", {{"version", "1.0.0"}, {"server", "http://127.0.0.1:1"}});
        CHECK(!download.ok);
        CHECK(Now(mcp, "hub_status").data["download"].is_null());
    }

    SECTION("Hub residente: porta interna, token do editor e avisos");
    {
        // MCP do hub ligado de verdade (HTTP) e uma versão que entende o --hub.
        WriteText(versionDir / hub::kMcpToolsFile,
                  R"({"format": "motor-mcp-tools", "version": 1, "hubLink": 1, "tools": []})");
        std::string error;
        int hubPort = 0;
        for (int port = 47660; port < 47670 && !hubPort; ++port) {
            if (mcp.Start(port, [] {}, error)) hubPort = port;
        }
        CHECK(hubPort > 0);

        const Call open = Wait(mcp, "project_open", {{"path", projectUtf8}, {"wait", true}, {"timeout", 15}});
        CHECK(open.ok);
        CHECK(open.data["ready"] == true);
        const Call status = Now(mcp, "hub_status");
        const Json& editor = status.data["editors"][0];
        CHECK(editor["linked"] == true && editor["ready"] == true);
        const int port = editor.value("mcpPort", 0);
        CHECK(port >= link::kEditorPortFirst && port <= link::kEditorPortLast);
        CHECK(Now(mcp, "projects_list").data[0]["editorReady"] == true);

        // Token errado: o hub recusa (só os editores que ele abriu avisam).
        // O aviso é atendido no Pump: manda de outra thread enquanto esta faz o papel do loop do hub.
        std::string refused;
        bool posted = true;
        std::atomic<bool> done = false;
        std::thread client([&] {
            posted = link::PostJson(hubPort, link::kNotifyPath, "token-inventado", {{"event", link::kEventClosing}},
                                    &refused);
            done = true;
        });
        while (!done) {
            mcp.Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        client.join();
        CHECK(!posted && refused.find("401") != std::string::npos);
        CHECK(Now(mcp, "hub_status").data["editors"][0]["ready"] == true);  // continua pronto
        CHECK(Wait(mcp, "editor_close", {{"path", projectUtf8}, {"force", true}}).ok);

        // O editor abre outro projeto (Arquivo > Abrir projeto): o hub segue e o põe na lista.
        const fs::path other = dir / hub::PathFromUtf8("Outro Jogo Ação");
        fs::create_directories(other);
        WriteText(other / "Outro.mproject",
                  R"({"format": "motor-project", "name": "Outro Jogo Ação", "engineVersion": "0.9.0"})");
        SetEnvironment("MOTOR_TESTE_OUTRO_PROJETO", hub::ToUtf8(other));
        CHECK(Wait(mcp, "project_open", {{"path", projectUtf8}, {"wait", true}, {"timeout", 15}}).ok);
        SetEnvironment("MOTOR_TESTE_OUTRO_PROJETO", "");
        bool followed = false;
        for (int i = 0; i < 200 && !followed; ++i) {  // o "projeto" chega logo depois do "pronto"
            mcp.Pump();
            followed = app.FindEditor(other) != nullptr;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK(followed);
        CHECK(!app.FindEditor(project));
        const Call list = Now(mcp, "projects_list", {{"query", "outro"}});
        CHECK(list.data.size() == 1 && list.data[0]["editorOpen"] == true);
        CHECK(Wait(mcp, "editor_close", {{"path", hub::ToUtf8(other)}, {"force", true}}).ok);

        // Versão sem "hubLink" (anterior ao Hub residente): abre como antes, sem a ligação, e wait explica.
        WriteText(versionDir / hub::kMcpToolsFile, R"({"format": "motor-mcp-tools", "version": 1, "tools": []})");
        const Call old = Now(mcp, "project_open", {{"path", projectUtf8}, {"wait", true}});
        CHECK(!old.ok && old.text.find("não avisa o Hub") != std::string::npos);
        CHECK(Now(mcp, "hub_status").data["editors"][0]["linked"] == false);
        CHECK(Wait(mcp, "editor_close", {{"path", projectUtf8}, {"force", true}}).ok);
        mcp.Stop();
    }

    SECTION("version_remove");
    {
        CHECK(!Now(mcp, "version_remove", {{"version", "5.0.0"}}).ok);
        // Remover de verdade manda para a Lixeira do usuário: fica fora do teste automático.
    }

    return TestResult();
}
