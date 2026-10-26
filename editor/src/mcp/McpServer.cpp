// httplib inclui o winsock2.h: precisa vir antes de qualquer windows.h.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif
#include <httplib.h>

#include "McpServer.h"

#include <motor/core/Log.h>

#include <atomic>
#include <chrono>
#include <future>
#include <utility>

namespace editor::mcp {

namespace {
// Tempo máximo esperando a thread principal (um quadro dura ~16 ms; compilar ou carregar algo pode demorar).
constexpr auto kMainThreadTimeout = std::chrono::seconds(60);
// Pedido que a ferramenta mandou esperar (wait = true): ela mesma tem um tempo limite; este é só a rede de
// segurança para o cliente não ficar preso para sempre.
constexpr auto kParkedTimeout = std::chrono::minutes(10);

int64_t Ticks(std::chrono::steady_clock::time_point t) {
    return t.time_since_epoch().count();
}

std::string BusyError() {
    return R"json({"jsonrpc":"2.0","id":null,"error":{"code":-32603,"message":"o editor não respondeu a tempo (ocupado ou fechando)"}})json";
}

// Pedido vindo de uma página da web (DNS rebinding, CSRF): só aceitamos origem local.
bool IsLocalOrigin(const std::string& value) {
    if (value.empty() || value == "null") return value.empty();
    for (const char* prefix : {"http://127.0.0.1", "http://localhost", "https://127.0.0.1", "https://localhost"}) {
        const std::string p = prefix;
        if (value.compare(0, p.size(), p) == 0 && (value.size() == p.size() || value[p.size()] == ':')) return true;
    }
    return false;
}

bool IsLocalHost(const std::string& host) {
    const std::string name = host.substr(0, host.rfind(':'));
    return host.empty() || name == "127.0.0.1" || name == "localhost" || name == "[::1]";
}
} // namespace

struct Server::Job {
    std::string body;
    std::promise<std::string> done;
    // Até quando a thread do HTTP espera (steady_clock); cresce quando a resposta fica para depois.
    std::atomic<int64_t> deadline{0};
};

struct Server::Parked {
    std::shared_ptr<Job> job;
    Deferred deferred;
};

Server::Server(Protocol& protocol) : m_protocol(protocol) {}

Server::~Server() {
    Stop();
}

std::string Server::Url() const {
    return "http://127.0.0.1:" + std::to_string(m_port) + "/mcp";
}

bool Server::Start(int port, std::string& error) {
    Stop();
    auto http = std::make_unique<httplib::Server>();
    http->set_payload_max_length(16 * 1024 * 1024);

    http->Post("/mcp", [this](const httplib::Request& req, httplib::Response& res) {
        if (!IsLocalOrigin(req.get_header_value("Origin")) || !IsLocalHost(req.get_header_value("Host"))) {
            res.status = 403;
            res.set_content("origem não permitida", "text/plain; charset=utf-8");
            return;
        }
        const std::string reply = Submit(req.body);
        if (reply.empty()) {
            res.status = 202;  // notificação: nada a responder
            return;
        }
        res.set_content(reply, "application/json");
    });
    // Sem fluxo SSE do servidor para o cliente (não mandamos notificações nem pedidos): 405, como manda o protocolo.
    http->Get("/mcp", [](const httplib::Request&, httplib::Response& res) { res.status = 405; });
    http->Delete("/mcp", [](const httplib::Request&, httplib::Response& res) { res.status = 405; });

    if (!http->bind_to_port("127.0.0.1", port)) {
        error = "não foi possível abrir a porta " + std::to_string(port) + " (outro programa ou outro editor usando?)";
        return false;
    }
    m_port = port;
    m_http = std::move(http);
    m_thread = std::thread([server = m_http.get()] { server->listen_after_bind(); });
    motor::log::Info("Servidor MCP ligado em {}", Url());
    return true;
}

void Server::Stop() {
    if (!m_http) return;
    m_http->stop();  // os pedidos esperando a thread principal recebem "ocupado" abaixo
    {
        std::lock_guard lock(m_mutex);
        for (auto& job : m_jobs) job->done.set_value(BusyError());
        m_jobs.clear();
    }
    for (Parked& p : m_parked) p.job->done.set_value(BusyError());
    m_parked.clear();
    if (m_thread.joinable()) m_thread.join();
    m_http.reset();
    motor::log::Info("Servidor MCP desligado");
}

std::string Server::Submit(std::string body) {
    auto job = std::make_shared<Job>();
    job->body = std::move(body);
    std::future<std::string> reply = job->done.get_future();
    {
        std::lock_guard lock(m_mutex);
        if (!m_http || !m_http->is_running()) return BusyError();
        m_jobs.push_back(job);
    }
    if (m_wake) m_wake();
    job->deadline = Ticks(std::chrono::steady_clock::now() + kMainThreadTimeout);
    while (reply.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
        if (Ticks(std::chrono::steady_clock::now()) < job->deadline) continue;
        std::lock_guard lock(m_mutex);
        if (reply.wait_for(std::chrono::seconds(0)) == std::future_status::ready) break;  // respondeu agora
        std::erase(m_jobs, job);  // se ainda não começou, não roda mais
        return BusyError();
    }
    return reply.get();
}

size_t Server::Pump() {
    // Primeiro os que esperam (Play começar, compilação acabar): um quadro já passou desde a última conferência.
    size_t handled = std::erase_if(m_parked, [](Parked& p) {
        std::optional<std::string> reply = p.deferred.Poll();
        if (!reply) return false;
        p.job->done.set_value(std::move(*reply));
        return true;
    });

    std::deque<std::shared_ptr<Job>> jobs;
    {
        std::lock_guard lock(m_mutex);
        jobs.swap(m_jobs);
    }
    for (auto& job : jobs) {
        std::optional<Deferred> deferred;
        std::string reply = m_protocol.HandleText(job->body, &deferred);
        if (deferred) {
            // Antes de a thread do HTTP desistir (kMainThreadTimeout), ela passa a esperar mais.
            job->deadline = Ticks(std::chrono::steady_clock::now() + kParkedTimeout);
            m_parked.push_back({job, std::move(*deferred)});
            continue;
        }
        job->done.set_value(std::move(reply));
    }
    return handled + jobs.size();
}

} // namespace editor::mcp
