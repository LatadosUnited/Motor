#pragma once

#include "McpProtocol.h"

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace httplib {
class Server;
}

namespace editor::mcp {

// Transporte "Streamable HTTP" do MCP: POST http://127.0.0.1:<porta>/mcp com uma mensagem JSON-RPC, resposta
// em application/json. O servidor HTTP roda numa thread própria, mas as mensagens são atendidas na thread
// principal do editor (Pump, uma vez por quadro), porque as ferramentas mexem na cena e no OpenGL.
//
// Segurança: só escuta em 127.0.0.1 e recusa pedidos com Origin/Host de fora (um site aberto no navegador
// não consegue chamar o editor). Não há senha: qualquer programa desta máquina pode usar enquanto estiver ligado.
class Server {
public:
    static constexpr int kDefaultPort = 47800;

    explicit Server(Protocol& protocol);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool Start(int port, std::string& error);  // false se a porta não abriu
    void Stop();
    bool IsRunning() const { return m_http != nullptr; }
    int Port() const { return m_port; }
    std::string Url() const;

    void Pump();  // thread principal: atende as mensagens que chegaram e confere as que esperam (wait)

private:
    struct Job;
    struct Parked;  // pedido com resposta para depois (ToolResult::later)

    std::string Submit(std::string body);  // thread do HTTP: espera a thread principal responder

    Protocol& m_protocol;
    std::unique_ptr<httplib::Server> m_http;
    std::thread m_thread;
    int m_port = 0;

    std::mutex m_mutex;
    std::deque<std::shared_ptr<Job>> m_jobs;
    std::vector<Parked> m_parked;  // só na thread principal
};

} // namespace editor::mcp
