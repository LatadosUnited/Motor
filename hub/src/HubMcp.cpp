#include "HubMcp.h"

#include "HubApp.h"

#include <Download.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <future>
#include <memory>

#ifndef MOTOR_HUB_VERSION
#define MOTOR_HUB_VERSION "0.0.0"
#endif

namespace hub {

using motor::mcp::Json;
using motor::mcp::Prop;
using motor::mcp::Schema;
using motor::mcp::Tool;
using motor::mcp::ToolError;
using motor::mcp::ToolResult;

namespace {

constexpr const char* kInstructions =
    "Hub do Motor: projetos e versões do Motor instaladas nesta máquina. Abra um projeto com project_open; o MCP do "
    "editor de cada projeto fica em /mcpengine/<Projeto> no mesmo endereço.";

using Clock = std::chrono::steady_clock;

Clock::time_point Deadline(double seconds) {
    return Clock::now() + std::chrono::milliseconds(static_cast<int64_t>(seconds * 1000.0));
}

std::string RequiredText(const Json& args, const char* name) {
    if (!args.contains(name) || !args[name].is_string() || args[name].get<std::string>().empty()) {
        throw ToolError(std::format("falta o argumento '{}' (texto)", name));
    }
    return args[name].get<std::string>();
}

fs::path RequiredPath(const Json& args, const char* name) {
    return PathFromUtf8(RequiredText(args, name));
}

ToolResult Outcome(bool ok, const std::string& message) {
    if (!ok) throw ToolError(message);
    return ToolResult::Text(message);
}

// Lê os servidores numa thread (servidor fora do ar não trava o hub); o `later` confere a cada volta do loop.
using FetchFuture = std::shared_ptr<std::future<std::vector<HubApp::RemoteServer>>>;

FetchFuture StartFetch(std::vector<std::string> urls) {
    return std::make_shared<std::future<std::vector<HubApp::RemoteServer>>>(
        std::async(std::launch::async, [urls = std::move(urls)] { return HubApp::FetchServers(urls); }));
}

bool Ready(const FetchFuture& f) {
    return f->wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

} // namespace

HubMcp::HubMcp(HubApp& app)
    : m_app(app), m_protocol("motor-hub", MOTOR_HUB_VERSION, kInstructions), m_server(m_protocol) {
    AddTools();
}

bool HubMcp::Start(int port, std::function<void()> wake, std::string& error) {
    m_token = LoadOrCreateMcpToken(m_app.GetPaths().mcpTokenFile, &error);
    if (m_token.empty()) {
        error = "MCP do Hub sem token: " + error;
        return false;  // sem token não liga: a porta ficaria aberta para qualquer programa
    }
    m_server.SetPath(kHubMcpPath);
    m_server.SetToken(m_token);
    m_server.SetSessions(true);
    m_server.SetWake(std::move(wake));
    if (!m_server.Start(port, error)) {
        error = "MCP do Hub desligado: " + error;
        return false;
    }
    return true;
}

void HubMcp::Stop() {
    m_server.Stop();
}

std::string HubMcp::ClaudeCommand() const {
    return std::format("claude mcp add --transport http motor-hub {} --header \"Authorization: Bearer {}\"", Url(),
                       m_token);
}

Json HubMcp::EditorsJson() {
    Json out = Json::array();
    for (const HubApp::OpenEditor& e : m_app.Editors()) {
        out.push_back({{"project", ToUtf8(e.root)},
                       {"version", e.version},
                       {"pid", e.process.Pid()},
                       {"opened", FormatLastOpened(e.opened, NowSeconds())}});
    }
    return out;
}

void HubMcp::AddTools() {
    AddStateTools();
    AddProjectTools();
    AddVersionTools();
}

void HubMcp::AddStateTools() {
    m_protocol.AddTool({
        .name = "hub_status",
        .description = "Estado do Hub do Motor: versão, pasta de dados, versões do Motor instaladas (e a padrão), "
                       "quantos projetos há na lista, os editores que o Hub abriu e o download em andamento.",
        .inputSchema = Schema(),
        .readOnly = true,
        .run =
            [this](const Json&) {
                const InstalledVersion* def = DefaultVersion(m_app.Versions(), m_app.Config().defaultVersion);
                Json versions = Json::array();
                for (const InstalledVersion& v : m_app.Versions()) versions.push_back(v.version);
                Json download = nullptr;
                if (m_app.m_download) {
                    std::lock_guard lock(m_app.m_download->mutex);
                    const distrib::ReleaseProgress& p = m_app.m_download->progress;
                    download = {{"version", m_app.m_download->release.version},
                                {"server", m_app.m_download->server},
                                {"bytesDone", p.done},
                                {"bytesTotal", p.total}};
                }
                return ToolResult::Data({
                    {"hubVersion", MOTOR_HUB_VERSION},
                    {"dataDir", ToUtf8(m_app.GetPaths().dataDir)},
                    {"versions", std::move(versions)},
                    {"defaultVersion", def ? def->version : ""},
                    {"projects", m_app.Projects().size()},
                    {"editors", EditorsJson()},
                    {"download", std::move(download)},
                    {"startsWithWindows", m_app.StartsWithWindows()},
                });
            },
    });
}

void HubMcp::AddProjectTools() {
    const Json pathProp = Prop("string", "pasta do projeto (a que tem o .mproject) ou o próprio .mproject");
    const Json engineProp =
        Prop("string", "versão do Motor (ex.: \"0.3.0\"); sem ela, a do projeto (ou a padrão para um novo)");

    m_protocol.AddTool({
        .name = "projects_list",
        .description = "Projetos da lista do Hub, dos abertos por último para os mais antigos: nome, pasta, versão do "
                       "Motor, última abertura e se o editor dele está aberto (aberto pelo Hub).",
        .inputSchema = Schema({{"query", Prop("string", "filtra por nome ou pasta (opcional)")}}),
        .readOnly = true,
        .run =
            [this](const Json& args) {
                // Na bandeja a janela não desenha (e não relê a lista): um projeto recém-criado já aparece aqui.
                m_app.RefreshProjects();
                const std::string query = args.value("query", std::string());
                Json out = Json::array();
                for (size_t i = 0; i < m_app.Projects().size(); ++i) {
                    const ProjectInfo& p = m_app.Projects()[i];
                    if (!MatchesSearch(p.name, query) && !MatchesSearch(ToUtf8(p.root), query)) continue;
                    Json item = {{"name", p.name},
                                 {"path", ToUtf8(p.root)},
                                 {"found", p.found},
                                 {"engineVersion", p.engineVersion},
                                 {"lastOpened", FormatLastOpened(m_app.Config().projects[i].lastOpened, NowSeconds())}};
                    if (!p.found) item["error"] = p.error;
                    const HubApp::OpenEditor* editor = m_app.FindEditor(p.root);
                    item["editorOpen"] = editor != nullptr;
                    if (editor) item["editorPid"] = editor->process.Pid();
                    out.push_back(std::move(item));
                }
                return ToolResult::Data(out);
            },
    });

    m_protocol.AddTool({
        .name = "project_open",
        .description = "Abre o projeto no editor da versão dele (como o Abrir do Hub). Se a versão do projeto não "
                       "estiver instalada, falha dizendo qual usar em 'engine' (o editor passa o projeto para essa "
                       "versão: faça uma cópia de segurança antes). Um projeto já aberto pelo Hub não abre de novo.",
        .inputSchema = Schema({{"path", pathProp}, {"engine", engineProp}}, {"path"}),
        .run =
            [this](const Json& args) {
                const fs::path root = RequiredPath(args, "path");
                std::string message;
                const bool ok = m_app.OpenProjectAuto(root, args.value("engine", std::string()), message);
                if (!ok) throw ToolError(message);
                const ProjectInfo info = ReadProject(root);
                const HubApp::OpenEditor* editor = m_app.FindEditor(info.root);
                return ToolResult::Data({{"message", message}, {"pid", editor ? editor->process.Pid() : 0}});
            },
    });

    m_protocol.AddTool({
        .name = "project_create",
        .description = "Cria um projeto novo com o editor de uma versão (como o Novo projeto do Hub): o editor abre "
                       "com o projeto criado em <parent>/<name>.",
        .inputSchema = Schema({{"name", Prop("string", "nome do projeto (vira o nome da pasta)")},
                               {"parent", Prop("string", "pasta onde criar (padrão: a última usada, ou "
                                                         "Documentos\\Motor)")},
                               {"engine", engineProp}},
                              {"name"}),
        .run =
            [this](const Json& args) {
                const std::string name = RequiredText(args, "name");
                fs::path parent = PathFromUtf8(args.value("parent", std::string())).lexically_normal();
                if (parent.empty()) {
                    parent = m_app.Config().newProjectDir.empty() ? m_app.GetPaths().defaultProjectsDir
                                                                  : m_app.Config().newProjectDir;
                }
                std::string message;
                const InstalledVersion* v = m_app.PickVersion(args.value("engine", std::string()), message);
                if (!v) throw ToolError(message);
                const bool ok = m_app.CreateProject(parent, name, *v, message);
                if (!ok) throw ToolError(message);
                return ToolResult::Data({{"message", message}, {"path", ToUtf8(parent / PathFromUtf8(name))}});
            },
    });

    m_protocol.AddTool({
        .name = "project_add",
        .description = "Põe na lista do Hub um projeto que já existe no disco.",
        .inputSchema = Schema({{"path", pathProp}}, {"path"}),
        .run =
            [this](const Json& args) {
                std::string message;
                const bool ok = m_app.AddProjectFolder(RequiredPath(args, "path"), message);
                return Outcome(ok, message);
            },
    });

    m_protocol.AddTool({
        .name = "project_remove",
        .description = "Tira o projeto da lista do Hub. A pasta continua no disco, intocada.",
        .inputSchema = Schema({{"path", pathProp}}, {"path"}),
        .run =
            [this](const Json& args) {
                std::string message;
                const bool ok = m_app.RemoveProject(ReadProject(RequiredPath(args, "path")).root, message);
                return Outcome(ok, message);
            },
    });

    m_protocol.AddTool({
        .name = "editor_close",
        .description = "Fecha o editor de um projeto aberto pelo Hub. Sem 'force', é como o X da janela: com algo não "
                       "salvo, o editor pergunta e não fecha sozinho (salve antes pelo MCP do editor). Com "
                       "force = true, fecha na hora e o que não estava salvo se perde.",
        .inputSchema = Schema({{"path", pathProp},
                               {"force", Prop("boolean", "fechar na hora, sem salvar (padrão: false)")},
                               {"wait", Prop("boolean", "esperar o editor fechar (padrão: true)")},
                               {"timeout", Prop("number", "segundos de espera (padrão: 15)")}},
                              {"path"}),
        .run =
            [this](const Json& args) {
                const fs::path root = ReadProject(RequiredPath(args, "path")).root;
                std::string message;
                if (!m_app.CloseEditor(root, args.value("force", false), message)) throw ToolError(message);
                ToolResult now = ToolResult::Text(message);
                if (!args.value("wait", true)) return now;
                const auto deadline = Deadline(std::clamp(args.value("timeout", 15.0), 0.5, 120.0));
                now.later = [this, root, deadline](ToolResult& out) {
                    if (!m_app.FindEditor(root)) {
                        out = ToolResult::Text(std::format("Editor de '{}' fechado.", ToUtf8(root.filename())));
                        return true;
                    }
                    if (Clock::now() < deadline) return false;
                    throw ToolError("o editor não fechou a tempo: pode estar perguntando se salva (salve pelo MCP "
                                    "dele) ou use force = true");
                };
                return now;
            },
    });
}

void HubMcp::AddVersionTools() {
    const Json versionProp = Prop("string", "versão do Motor, ex.: \"0.3.0\"");
    const Json serverProp = Prop("string", "URL de um servidor de versões (padrão: os da lista do Hub)");

    m_protocol.AddTool({
        .name = "versions_list",
        .description = "Versões do Motor instaladas: versão, API de scripts, tipo do build, pasta e qual é a padrão "
                       "(a dos projetos novos).",
        .inputSchema = Schema(),
        .readOnly = true,
        .run =
            [this](const Json&) {
                const InstalledVersion* def = DefaultVersion(m_app.Versions(), m_app.Config().defaultVersion);
                Json out = Json::array();
                for (const InstalledVersion& v : m_app.Versions()) {
                    out.push_back({{"version", v.version},
                                   {"scriptApiVersion", v.scriptApiVersion},
                                   {"buildType", v.buildType},
                                   {"dir", ToUtf8(v.dir)},
                                   {"default", &v == def}});
                }
                return ToolResult::Data(out);
            },
    });

    m_protocol.AddTool({
        .name = "version_set_default",
        .description = "Escolhe a versão padrão do Motor (a dos projetos novos).",
        .inputSchema = Schema({{"version", versionProp}}, {"version"}),
        .run =
            [this](const Json& args) {
                std::string message;
                const bool ok = m_app.SetDefaultVersion(RequiredText(args, "version"), message);
                return Outcome(ok, message);
            },
    });

    m_protocol.AddTool({
        .name = "version_remove",
        .description = "Manda uma versão do Motor instalada para a Lixeira (dá para restaurar de lá). Recusa se um "
                       "editor dessa versão estiver aberto pelo Hub. Projetos dessa versão vão pedir outra para abrir.",
        .inputSchema = Schema({{"version", versionProp}}, {"version"}),
        .run =
            [this](const Json& args) {
                const std::string version = RequiredText(args, "version");
                for (const HubApp::OpenEditor& e : m_app.Editors()) {
                    if (e.version == version) {
                        throw ToolError(std::format("o editor de '{}' está aberto no Motor {}: feche-o antes",
                                                    ToUtf8(e.root), version));
                    }
                }
                std::string message;
                const bool ok = m_app.RemoveVersion(version, message);
                return Outcome(ok, message);
            },
    });

    m_protocol.AddTool({
        .name = "versions_available",
        .description = "Versões do Motor publicadas nos servidores de versões (e um Hub mais novo, se houver), "
                       "dizendo quais já estão instaladas.",
        .inputSchema = Schema({{"server", serverProp}}),
        .readOnly = true,
        .run =
            [this](const Json& args) {
                const std::string server = args.value("server", std::string());
                std::vector<std::string> urls = server.empty() ? m_app.Config().servers : std::vector{server};
                if (urls.empty()) throw ToolError("nenhum servidor de versões na lista do Hub (passe 'server')");
                const FetchFuture fetch = StartFetch(std::move(urls));
                ToolResult now = ToolResult::Text("procurando...");
                const auto deadline = Deadline(60.0);
                now.later = [this, fetch, deadline](ToolResult& out) {
                    if (!Ready(fetch)) {
                        if (Clock::now() < deadline) return false;
                        throw ToolError("os servidores não responderam a tempo");
                    }
                    Json servers = Json::array();
                    for (const HubApp::RemoteServer& s : fetch->get()) {
                        Json item = {{"url", s.url}};
                        if (!s.manifest) {
                            item["error"] = s.error;
                            servers.push_back(std::move(item));
                            continue;
                        }
                        Json releases = Json::array();
                        for (const distrib::Release& r : s.manifest->releases) {
                            releases.push_back({{"version", r.version},
                                                {"channel", r.channel},
                                                {"scriptApiVersion", r.scriptApiVersion},
                                                {"buildType", r.buildType},
                                                {"size", distrib::FormatSize(r.size)},
                                                {"installed", FindVersion(m_app.Versions(), r.version) != nullptr}});
                        }
                        item["releases"] = std::move(releases);
                        if (s.manifest->hub) {
                            item["hub"] = {{"version", s.manifest->hub->version},
                                           {"newer", CompareVersions(s.manifest->hub->version, MOTOR_HUB_VERSION) > 0}};
                        }
                        servers.push_back(std::move(item));
                    }
                    out = ToolResult::Data(servers);
                    return true;
                };
                return now;
            },
    });

    m_protocol.AddTool({
        .name = "version_download",
        .description = "Baixa uma versão do Motor de um servidor de versões e a instala (cada arquivo conferido pelo "
                       "SHA-256; um download interrompido é retomado). O progresso aparece no hub_status e na aba "
                       "Versões. Um download por vez.",
        .inputSchema = Schema({{"version", Prop("string", "versão, ou \"mais-recente\"")},
                               {"server", serverProp},
                               {"wait", Prop("boolean", "esperar terminar (padrão: false; pode levar minutos)")},
                               {"timeout", Prop("number", "segundos de espera com wait (padrão: 600)")}},
                              {"version"}),
        .run =
            [this](const Json& args) {
                const std::string wanted = RequiredText(args, "version");
                const bool latest = wanted == "mais-recente" || wanted == "latest";
                const std::string server = args.value("server", std::string());
                std::vector<std::string> urls = server.empty() ? m_app.Config().servers : std::vector{server};
                if (urls.empty()) throw ToolError("nenhum servidor de versões na lista do Hub (passe 'server')");
                if (m_app.m_download) {
                    throw ToolError(std::format("já há um download em andamento (Motor {})",
                                                m_app.m_download->release.version));
                }
                const FetchFuture fetch = StartFetch(std::move(urls));
                const bool wait = args.value("wait", false);
                const auto fetchDeadline = Deadline(60.0);
                const auto deadline = Deadline(std::clamp(args.value("timeout", 600.0), 1.0, 3600.0));
                // 1. Lê os servidores e escolhe a versão; 2. começa o download da janela; 3. com wait, espera ele.
                auto chosen = std::make_shared<std::string>();
                ToolResult now = ToolResult::Text("procurando a versão nos servidores...");
                now.later = [this, fetch, latest, wanted, wait, fetchDeadline, deadline, chosen](ToolResult& out) {
                    if (chosen->empty()) {
                        if (!Ready(fetch)) {
                            if (Clock::now() < fetchDeadline) return false;
                            throw ToolError("os servidores não responderam a tempo");
                        }
                        std::string url, errors;
                        std::optional<distrib::Release> release;
                        for (const HubApp::RemoteServer& s : fetch->get()) {
                            if (!s.manifest) {
                                errors += " " + s.error;
                                continue;
                            }
                            const distrib::Release* r = latest ? s.manifest->Latest() : s.manifest->Find(wanted);
                            if (r && (!release || (latest && CompareVersions(r->version, release->version) > 0))) {
                                release = *r;
                                url = s.url;
                                if (!latest) break;
                            }
                        }
                        if (!release) {
                            throw ToolError((latest ? std::string("nenhuma versão publicada nos servidores")
                                                    : std::format("o Motor {} não está publicado nos servidores", wanted)) +
                                            errors);
                        }
                        if (FindVersion(m_app.Versions(), release->version)) {
                            throw ToolError(std::format("o Motor {} já está instalado", release->version));
                        }
                        if (m_app.m_download) throw ToolError("outro download começou nesse meio-tempo");
                        m_app.m_lastDownload.reset();
                        m_app.StartDownload(url, *release);
                        *chosen = release->version;
                        if (!wait) {
                            out = ToolResult::Text(std::format(
                                "Baixando o Motor {} de {} ({}): acompanhe pelo hub_status.", release->version, url,
                                distrib::FormatSize(release->size)));
                            return true;
                        }
                    }
                    if (m_app.m_lastDownload && m_app.m_lastDownload->version == *chosen) {
                        if (!m_app.m_lastDownload->ok) throw ToolError(m_app.m_lastDownload->message);
                        out = ToolResult::Text(m_app.m_lastDownload->message);
                        return true;
                    }
                    if (Clock::now() < deadline) return false;
                    throw ToolError("o download não terminou a tempo (continua em segundo plano; veja hub_status)");
                };
                return now;
            },
    });
}

} // namespace hub
