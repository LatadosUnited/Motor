#pragma once

// Servidor de versões do Motor (Fase 15), sem a linha de comando: serve uma pasta de publicações (Manifest.h)
// por HTTP com a cpp-httplib. Usado pelo MotorServidor.exe e, numa porta livre em 127.0.0.1, pelo
// tests/test_distribuicao.cpp.
//
//   GET /                              página com as versões e o link do instalador
//   GET /api/versoes                   manifesto (motor-releases)
//   GET /arquivos/<versão>/<caminho>   um arquivo de uma versão (aceita Range, para retomar)
//   GET /hub/latest                    Hub e instalador publicados (motor-hub-release)
//   GET /hub/MotorHub.exe, /hub/MotorSetup.exe
//
// Só serve arquivos que estão no manifesto: nenhum caminho do pedido chega ao disco sem estar na lista.

#include <Manifest.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace httplib {
class Server;
}

namespace distrib {

class DistribServer {
public:
    explicit DistribServer(std::filesystem::path root);
    ~DistribServer();
    DistribServer(const DistribServer&) = delete;
    DistribServer& operator=(const DistribServer&) = delete;

    // Escuta em `host` ("0.0.0.0" = rede local, "127.0.0.1" = só esta máquina). `port` 0 = uma porta livre.
    // Devolve a porta, ou -1 (com `error`).
    int Start(const std::string& host, int port, std::string* error = nullptr);
    void Stop();
    int Port() const { return m_port; }

    // Uma linha por pedido que não seja de arquivo (manifesto, páginas) e por erro.
    std::function<void(const std::string&)> log;

private:
    std::shared_ptr<const Manifest> CurrentManifest();  // relê o manifesto.json quando ele muda
    void Log(const std::string& line);

    std::filesystem::path m_root;
    std::unique_ptr<httplib::Server> m_http;
    std::thread m_thread;
    int m_port = -1;

    std::mutex m_mutex;
    std::shared_ptr<const Manifest> m_manifest;
    std::filesystem::file_time_type m_manifestTime{};
    uint64_t m_manifestSize = 0;
};

// Página "/" (testável): versões, link do instalador e como pôr o servidor no Hub. `host` é o cabeçalho Host
// do pedido (o endereço que quem abriu a página usou).
std::string RenderIndexPage(const Manifest& manifest, const std::string& host);

// Nome da pasta de publicações que fica ao lado do MotorServidor.exe numa instalação portátil (ex.: C:\MotorServidor).
inline constexpr const char* kPortablePublishFolder = "Publicacoes";

// Pasta de publicações quando não há --pasta: a `Publicacoes` ao lado do .exe, se ela existir (dois cliques no
// .exe servem o que foi publicado ali); senão `fallback` (%LOCALAPPDATA%\Motor\Servidor).
std::filesystem::path DefaultPublishRoot(const std::filesystem::path& exeDir, const std::filesystem::path& fallback);

} // namespace distrib
