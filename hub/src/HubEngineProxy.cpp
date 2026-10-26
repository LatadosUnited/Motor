// Hub residente: o MCP do editor de cada projeto pelo endereço fixo do Hub. /mcpengine/<Projeto> vai para o editor
// daquele projeto e /mcpengine, para o editor aberto por último. O Hub responde o initialize, o ping e o tools/list
// com a lista de ferramentas da versão (mcp-ferramentas.json), então o Claude Code conecta mesmo com o editor fechado;
// o resto (tools/call) é repassado ao editor na porta interna dele (HubLink.h), que o Hub abre se estiver fechado.
// O pedido repassado roda numa thread (a ferramenta pode demorar minutos); o loop do hub só confere se terminou.

#include "HubApp.h"
#include "HubMcp.h"

#include <motor/mcp/HubLink.h>

#include <chrono>
#include <cstring>
#include <format>
#include <atomic>
#include <thread>
#include <memory>

namespace hub {

using motor::mcp::Json;
using Reply = motor::mcp::Server::RouteReply;
namespace link = motor::mcp::hublink;

namespace {

constexpr auto kOpenTimeout = std::chrono::seconds(90);  // editor fechado: abrir, carregar o projeto e ligar o MCP
constexpr int kForwardTimeoutMs = 11 * 60 * 1000;         // mais que o maior wait de uma ferramenta do editor

Reply RpcResult(const Json& id, Json result) {
    return {200, Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}}.dump(), {}};
}

Reply RpcError(const Json& id, const std::string& message, int code = -32603) {
    return {200, Json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}}.dump(), {}};
}

} // namespace

Reply HubMcp::OnEngineRequest(const motor::mcp::Server::RouteRequest& request) {
    if (!link::SameSecret(request.authorization, "Bearer " + m_token)) {
        return {401, Json{{"error", "token ausente ou errado (Authorization: Bearer <token>)"}}.dump(), {}};
    }
    std::string name = request.path.substr(std::strlen(kHubEnginePath));
    while (!name.empty() && name.front() == '/') name.erase(name.begin());
    while (!name.empty() && name.back() == '/') name.pop_back();

    const Json message = Json::parse(request.body, nullptr, false);
    if (message.is_discarded()) return RpcError(nullptr, "JSON inválido", -32700);
    const bool single = message.is_object();
    const Json id = single ? message.value("id", Json()) : Json();
    if (single && !message.contains("id")) return {200, "", {}};  // notificação: 202, nada a repassar

    // --- Para onde vai: o projeto pelo nome, ou o editor aberto por último ---
    fs::path root;          // vazio: /mcpengine sem editor aberto
    std::string projectName;
    uint32_t pid = 0;       // editor aberto pelo hub (0 = fechado)
    std::string version;    // a lista de ferramentas vem dela
    if (name.empty()) {
        const HubApp::OpenEditor* last = nullptr;
        for (const HubApp::OpenEditor& e : m_app.Editors()) {
            if (e.mcpPort > 0 && (!last || e.opened >= last->opened)) last = &e;
        }
        if (last) {
            root = last->root;
            projectName = ToUtf8(last->root.filename());
            pid = last->process.Pid();
            version = last->version;
        } else if (const InstalledVersion* def = DefaultVersion(m_app.Versions(), m_app.Config().defaultVersion)) {
            version = def->version;  // o initialize e o tools/list respondem mesmo sem editor
        }
    } else {
        m_app.RefreshProjects();
        std::vector<const ProjectInfo*> matches;
        for (const ProjectInfo& p : m_app.Projects()) {
            if (SameProjectName(p.name, name)) matches.push_back(&p);
        }
        if (matches.empty()) {
            return RpcError(id, std::format("nenhum projeto '{}' na lista do Hub (projects_list no /mcphub mostra os "
                                            "nomes; o da URL é o nome do projeto)", name));
        }
        if (matches.size() > 1) {
            return RpcError(id, std::format("há {} projetos chamados '{}' na lista do Hub: renomeie um deles (o nome "
                                            "vai na URL)", matches.size(), name));
        }
        const ProjectInfo& project = *matches.front();
        if (!project.found) return RpcError(id, std::format("projeto '{}': {}", name, project.error));
        root = project.root;
        projectName = project.name;
        if (const HubApp::OpenEditor* open = m_app.FindEditor(root)) {
            pid = open->process.Pid();
            version = open->version;
        } else {
            const VersionChoice choice =
                ChooseVersion(project.engineVersion, m_app.Versions(), m_app.Config().defaultVersion);
            if (choice.version) version = choice.version->version;
        }
    }

    // --- O que o Hub responde sozinho, com a lista da versão ---
    const InstalledVersion* installed = FindVersion(m_app.Versions(), version);
    const std::optional<EditorTools> tools = installed ? ReadEditorTools(installed->dir) : std::nullopt;
    if (single && tools) {
        const std::string method = message.value("method", std::string());
        if (method == "initialize") {
            Json result = tools->initialize;
            const Json& params = message.contains("params") ? message["params"] : Json::object();
            result["protocolVersion"] =
                motor::mcp::Protocol::NegotiateVersion(params.is_object() ? params.value("protocolVersion", "") : "");
            return RpcResult(id, std::move(result));
        }
        if (method == "ping") return RpcResult(id, Json::object());
        if (method == "tools/list") return RpcResult(id, {{"tools", tools->tools}});
    }

    // --- Repassa ao editor (abrindo, se preciso) ---
    if (root.empty()) {
        return RpcError(id, "nenhum editor aberto pelo Hub: use /mcpengine/<Projeto> (o nome está no projects_list do "
                            "/mcphub) ou abra um com o project_open");
    }
    if (pid == 0) {
        std::string opened;
        if (!m_app.OpenProjectAuto(root, "", opened)) return RpcError(id, opened);
        const HubApp::OpenEditor* editor = m_app.FindEditor(root);
        if (!editor) return RpcError(id, "o editor fechou logo ao abrir");
        pid = editor->process.Pid();
    }

    // Thread solta, não std::async: o future do async trava no destrutor, e o hub fechando com um pedido repassado
    // em andamento ficaria preso até ele acabar.
    struct Forward {
        uint32_t pid = 0;
        std::chrono::steady_clock::time_point deadline;
        bool started = false;
        std::atomic<bool> done{false};
        link::HttpReply reply;  // escrita antes de `done`
    };
    auto state = std::make_shared<Forward>();
    state->pid = pid;
    state->deadline = std::chrono::steady_clock::now() + kOpenTimeout;
    Reply later;
    later.later = [this, state, id, body = request.body, projectName](Reply& out) {
        const HubApp::OpenEditor* editor = nullptr;
        for (const HubApp::OpenEditor& e : m_app.Editors()) {
            if (e.process.Pid() == state->pid) editor = &e;
        }
        if (!state->started) {
            if (!editor) {
                out = RpcError(id, std::format("o editor de '{}' fechou antes de responder", projectName));
                return true;
            }
            if (editor->mcpPort == 0) {
                out = RpcError(id, std::format("o editor de '{}' foi aberto sem a ligação com o Hub (versão do Motor "
                                               "anterior ao Hub residente, ou aberto antes de o MCP do Hub ligar): "
                                               "feche-o (editor_close) e chame de novo", projectName));
                return true;
            }
            if (!editor->ready) {
                if (std::chrono::steady_clock::now() < state->deadline) return false;  // ainda abrindo
                out = RpcError(id, std::format("o editor de '{}' não ficou pronto a tempo", projectName));
                return true;
            }
            state->started = true;
            std::thread([state, wake = m_wake, port = editor->mcpPort, token = editor->token, body] {
                state->reply = link::Post(port, "/mcp", token, body, kForwardTimeoutMs);
                state->done = true;
                if (wake) wake();  // o hub parado dorme: confere a resposta já, não na próxima volta
            }).detach();
        }
        if (!state->done) return false;
        const link::HttpReply& reply = state->reply;
        if (reply.status == 0) {
            out = RpcError(id, std::format("o editor de '{}' não respondeu: {}", projectName, reply.error));
            return true;
        }
        out = {reply.status, reply.body, {}};
        return true;
    };
    return later;
}

} // namespace hub
