#pragma once

// Cliente HTTP dos servidores de versões (Fase 15), com a cpp-httplib: manifesto, Hub publicado e download de
// uma versão inteira, arquivo por arquivo, conferindo o SHA-256 e retomando o que já tinha sido baixado (Range).
//
// A versão é baixada em <Versões>/.baixando/<versão>/ (mesmo disco: o fim é só renomear) e só vira versão
// instalada quando todos os arquivos conferem. Cancelar deixa a pasta parcial para retomar depois.

#include "Manifest.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace httplib {
class Client;
}

namespace distrib {

enum class Status { Ok, Cancelled, Failed };

class Client {
public:
    explicit Client(std::string_view serverUrl);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    bool Valid() const { return m_url.has_value(); }
    const std::string& UrlError() const { return m_urlError; }
    const std::string& Url() const { return m_display; }

    std::optional<std::string> GetText(std::string_view path, std::string* error = nullptr);
    std::optional<Manifest> FetchManifest(std::string* error = nullptr);
    std::optional<HubRelease> FetchHubRelease(std::string* error = nullptr);

    struct FileResult {
        Status status = Status::Failed;
        bool resumed = false;      // continuou de um arquivo parcial (Range)
        uint64_t received = 0;     // bytes que vieram pela rede
        std::string error;
    };
    // Baixa `urlPath` (já relativo ao servidor, ex. "/arquivos/0.2.0/libMotor.dll") para `dest`, conferindo o
    // tamanho e o SHA-256 de `expected`. Se `dest` já tiver parte do arquivo, pede só o resto; se já estiver
    // completo e conferir, não baixa nada. `onFileBytes` recebe quantos bytes do arquivo já estão no disco.
    FileResult DownloadFile(std::string_view urlPath, const fs::path& dest, const ReleaseFile& expected,
                            const std::atomic<bool>* cancel = nullptr,
                            const std::function<void(uint64_t)>& onFileBytes = nullptr);

private:
    FileResult DownloadOnce(const std::string& path, const fs::path& dest, const ReleaseFile& expected,
                            const std::atomic<bool>* cancel, const std::function<void(uint64_t)>& onFileBytes);

    std::optional<ServerUrl> m_url;
    std::string m_urlError;
    std::string m_display;
    std::unique_ptr<httplib::Client> m_http;
};

std::string FileUrl(std::string_view version, std::string_view path);  // "/arquivos/<versão>/<caminho>", codificado
std::string HubFileUrl(std::string_view name);                         // "/hub/<nome>"

struct ReleaseProgress {
    uint64_t done = 0;   // bytes no disco (já baixados antes contam)
    uint64_t total = 0;
    size_t filesDone = 0;
    size_t filesTotal = 0;
    std::string currentFile;
};

struct ReleaseOutcome {
    Status status = Status::Failed;
    std::string error;
    fs::path dir;             // <Versões>/<versão>, se Ok
    uint64_t received = 0;    // bytes que vieram pela rede
    uint64_t reused = 0;      // bytes aproveitados de um download anterior
};

fs::path StagingRoot(const fs::path& versionsRoot);  // <Versões>/.baixando
fs::path StagingDir(const fs::path& versionsRoot, std::string_view version);
// Quanto de `release` já está na pasta de download parcial (para mostrar "Retomar (40%)").
uint64_t StagedBytes(const fs::path& versionsRoot, const Release& release);

// Baixa a versão inteira e instala em <versionsRoot>/<versão> (renomeando a pasta parcial no fim). Falha se a
// versão já estiver instalada. `progress` é chamado da mesma thread, a cada pedaço recebido.
ReleaseOutcome DownloadRelease(std::string_view serverUrl, const Release& release, const fs::path& versionsRoot,
                               const std::atomic<bool>* cancel = nullptr,
                               const std::function<void(const ReleaseProgress&)>& progress = nullptr);

} // namespace distrib
