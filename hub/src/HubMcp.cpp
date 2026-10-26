#include "HubMcp.h"

#include "HubApp.h"

#include <format>

#ifndef MOTOR_HUB_VERSION
#define MOTOR_HUB_VERSION "0.0.0"
#endif

namespace hub {

using motor::mcp::Json;
using motor::mcp::Schema;
using motor::mcp::ToolResult;

namespace {
constexpr const char* kInstructions =
    "Hub do Motor: projetos e versões do Motor instaladas nesta máquina. O MCP do editor de cada projeto fica em "
    "/mcpengine/<Projeto> no mesmo endereço.";
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

void HubMcp::AddTools() {
    m_protocol.AddTool({
        .name = "hub_status",
        .description = "Estado do Hub do Motor: versão, pasta de dados, versões do Motor instaladas (e a padrão) e "
                       "quantos projetos estão na lista.",
        .inputSchema = Schema(),
        .readOnly = true,
        .run =
            [this](const Json&) {
                const InstalledVersion* def = DefaultVersion(m_app.Versions(), m_app.Config().defaultVersion);
                Json versions = Json::array();
                for (const InstalledVersion& v : m_app.Versions()) versions.push_back(v.version);
                return ToolResult::Data({
                    {"hubVersion", MOTOR_HUB_VERSION},
                    {"dataDir", ToUtf8(m_app.GetPaths().dataDir)},
                    {"versions", std::move(versions)},
                    {"defaultVersion", def ? def->version : ""},
                    {"projects", m_app.Projects().size()},
                    {"startsWithWindows", m_app.StartsWithWindows()},
                });
            },
    });
}

} // namespace hub
