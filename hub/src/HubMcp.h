#pragma once

// MCP do Hub (Hub residente): http://127.0.0.1:47600/mcphub, sempre ligado enquanto o hub está aberto (na janela ou
// na bandeja), com o token de <dados>\mcp-token e sessão. Os pedidos chegam na thread do HTTP e são atendidos em
// Pump, na thread da janela do hub, como os botões: as ferramentas mexem no mesmo estado do HubApp.

#include <motor/mcp/McpProtocol.h>
#include <motor/mcp/McpServer.h>

#include <functional>
#include <string>

namespace hub {

class HubApp;

class HubMcp {
public:
    explicit HubMcp(HubApp& app);

    // `wake`: chamado na thread do HTTP quando chega um pedido (o hub parado dorme no glfwWaitEvents).
    bool Start(int port, std::function<void()> wake, std::string& error);
    void Stop();
    void Pump() { m_server.Pump(); }

    bool IsRunning() const { return m_server.IsRunning(); }
    std::string Url() const { return m_server.Url(); }
    const std::string& Token() const { return m_token; }
    // Linha para o Claude Code ligar neste servidor (claude mcp add ...).
    std::string ClaudeCommand() const;

private:
    void AddTools();

    HubApp& m_app;
    motor::mcp::Protocol m_protocol;
    motor::mcp::Server m_server;
    std::string m_token;
};

} // namespace hub
