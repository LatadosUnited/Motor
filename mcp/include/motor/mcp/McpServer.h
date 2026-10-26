#pragma once

#include <motor/mcp/McpProtocol.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace httplib {
class Server;
}

namespace motor::mcp {

// Transporte "Streamable HTTP" do MCP: POST http://127.0.0.1:<porta>/mcp com uma mensagem JSON-RPC, resposta
// em application/json. O servidor HTTP roda numa thread própria, mas as mensagens são atendidas na thread
// principal de quem o usa (Pump, uma vez por quadro): no editor, porque as ferramentas mexem na cena e no OpenGL.
// Não depende da engine (o Hub também usa): o log vai para quem chamar SetLog.
//
// Segurança: só escuta em 127.0.0.1 e recusa pedidos com Origin/Host de fora (um site aberto no navegador
// não consegue chamar o editor). Sem token (o editor), qualquer programa desta máquina pode usar enquanto estiver
// ligado; com SetToken (o Hub, sempre ligado), o pedido precisa de "Authorization: Bearer <token>".
//
// Sessão (SetSessions): o initialize ganha um Mcp-Session-Id e o DELETE encerra. Um id desconhecido é aceito
// mesmo assim: o servidor não guarda nada por sessão, e assim o cliente não precisa reconectar quando quem serve
// reinicia (o Hub atualizado, por exemplo).
class Server {
public:
    static constexpr int kDefaultPort = 47800;

    explicit Server(Protocol& protocol);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Antes do Start.
    void SetPath(std::string path) { m_path = std::move(path); }  // padrão "/mcp"
    void SetToken(std::string token) { m_token = std::move(token); }  // vazio = sem token
    void SetSessions(bool on) { m_sessions = on; }

    bool Start(int port, std::string& error);  // false se a porta não abriu
    void Stop();
    bool IsRunning() const { return m_http != nullptr; }
    int Port() const { return m_port; }
    std::string Url() const;

    // Thread principal: atende as mensagens que chegaram e confere as que esperam (wait). Devolve quantas atendeu.
    size_t Pump();
    bool HasParked() const { return !m_parked.empty(); }  // pedidos esperando algo (Play, compilação)
    // Chamado na thread do HTTP quando chega um pedido: o editor parado dorme no WaitEvents e precisa acordar.
    void SetWake(std::function<void()> wake) { m_wake = std::move(wake); }
    // Mensagens de ligado e desligado (o editor manda para o motor::log).
    void SetLog(std::function<void(const std::string&)> log) { m_log = std::move(log); }

private:
    struct Job;
    struct Parked;  // pedido com resposta para depois (ToolResult::later)

    std::string Submit(std::string body);  // thread do HTTP: espera a thread principal responder

    Protocol& m_protocol;
    std::unique_ptr<httplib::Server> m_http;
    std::thread m_thread;
    int m_port = 0;
    std::function<void()> m_wake;
    std::function<void(const std::string&)> m_log;
    std::string m_path = "/mcp";
    std::string m_token;
    bool m_sessions = false;
    std::atomic<uint64_t> m_sessionCount{0};  // para o id ser único mesmo com o relógio igual

    std::mutex m_mutex;
    std::deque<std::shared_ptr<Job>> m_jobs;
    std::vector<Parked> m_parked;  // só na thread principal
};

} // namespace motor::mcp
