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
    bool HasParked() const { return m_server.HasParked(); }  // pedidos esperando (download, editor fechando)
    motor::mcp::Protocol& GetProtocol() { return m_protocol; }  // testes: chamar as ferramentas sem HTTP

    bool IsRunning() const { return m_server.IsRunning(); }
    int Port() const { return m_server.Port(); }
    std::string Url() const { return m_server.Url(); }
    const std::string& Token() const { return m_token; }
    // Linha para o Claude Code ligar neste servidor (claude mcp add ...).
    std::string ClaudeCommand() const;
    // MCP do editor pelo Hub: /mcpengine/<projeto> (vazio = /mcpengine, o editor aberto por último) e a linha do
    // Claude Code para ele (servidor "motor").
    std::string EngineUrl(const std::string& projectName = {}) const;
    std::string ClaudeEngineCommand(const std::string& projectName = {}) const;

private:
    void AddTools();
    void AddStateTools();
    void AddProjectTools();
    void AddVersionTools();
    void AddUpdateTools();  // autoatualização do Hub (hub_update_check, hub_update)
    motor::mcp::Json EditorsJson();
    // Aviso de um editor aberto por este hub (POST em hublink::kNotifyPath), na thread do hub.
    motor::mcp::Server::RouteReply OnEditorNotice(const motor::mcp::Server::RouteRequest& request);
    // /mcpengine e /mcpengine/<Projeto> (HubEngineProxy.cpp): initialize e tools/list da lista da versão, o resto
    // repassado ao editor do projeto, que abre se estiver fechado.
    motor::mcp::Server::RouteReply OnEngineRequest(const motor::mcp::Server::RouteRequest& request);

    HubApp& m_app;
    motor::mcp::Protocol m_protocol;
    motor::mcp::Server m_server;
    std::string m_token;
    std::function<void()> m_wake;
};

} // namespace hub
