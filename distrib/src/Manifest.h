#pragma once

// Distribuição das versões do Motor (Fase 15): o manifesto que o servidor publica e o hub e o instalador leem,
// os caminhos e URLs, e a pasta de publicações do servidor. Sem rede nem janela (testado em
// tests/test_distribuicao.cpp).
//
// Pasta de publicações do MotorServidor:
//   manifesto.json                 Manifest (abaixo), servido em GET /api/versoes
//   versoes/<versão>/...           os arquivos da pasta montada pelo alvo motor_package, um a um, sem compressão
//   hub/MotorHub.exe, MotorSetup.exe
//
// Cada arquivo vai no manifesto com tamanho e SHA-256: quem baixa confere arquivo por arquivo e retoma um
// download interrompido pedindo só o que falta (Range). Sem zip: nenhuma biblioteca de compressão.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace distrib {

namespace fs = std::filesystem;

inline constexpr int kDefaultPort = 47900;
inline constexpr const char* kDefaultServer = "http://127.0.0.1:47900";
inline constexpr const char* kDefaultChannel = "estável";

inline constexpr const char* kHubExe = "MotorHub.exe";
inline constexpr const char* kSetupExe = "MotorSetup.exe";

// Rotas do servidor.
inline constexpr const char* kRouteManifest = "/api/versoes";
inline constexpr const char* kRouteFiles = "/arquivos/";  // /arquivos/<versão>/<caminho>
inline constexpr const char* kRouteHubLatest = "/hub/latest";
inline constexpr const char* kRouteHubFiles = "/hub/";  // /hub/MotorHub.exe, /hub/MotorSetup.exe

struct ReleaseFile {
    std::string path;  // relativo, com '/', UTF-8 ("engine/shaders/pbr.frag")
    uint64_t size = 0;
    std::string sha256;  // hexadecimal minúsculo
};

// Uma versão do Motor publicada.
struct Release {
    std::string version;
    std::string channel = kDefaultChannel;
    int scriptApiVersion = 0;
    std::string buildType;  // Debug, Release
    std::string date;       // ISO 8601 ("built" do motor-version.json)
    uint64_t size = 0;      // soma dos arquivos
    std::vector<ReleaseFile> files;
};

// O Hub e o instalador publicados (GET /hub/latest).
struct HubRelease {
    std::string version;
    std::string date;
    std::vector<ReleaseFile> files;  // MotorHub.exe e, se publicado, MotorSetup.exe

    const ReleaseFile* Find(std::string_view name) const;
};

struct Manifest {
    static constexpr const char* kFormat = "motor-releases";
    static constexpr int kVersion = 1;

    std::vector<Release> releases;  // da mais nova para a mais antiga
    std::optional<HubRelease> hub;

    // Leitura tolerante: campos ausentes = padrão; entradas inválidas (versão ruim, caminho inseguro) são
    // ignoradas. Falha só se não for JSON ou não for um manifesto do Motor.
    static std::optional<Manifest> Parse(std::string_view text, std::string* error = nullptr);
    std::string Dump() const;

    const Release* Find(std::string_view version) const;
    const Release* Latest(std::string_view channel = {}) const;  // canal vazio = qualquer
    void Put(Release release);  // substitui a mesma versão e reordena
    bool Remove(std::string_view version);
};

std::string HubReleaseJson(const HubRelease& hub);  // corpo de GET /hub/latest
std::optional<HubRelease> ParseHubRelease(std::string_view text, std::string* error = nullptr);

// --- Caminhos e URLs ----------------------------------------------------------------------------------------

// Caminho relativo seguro para gravar no disco de quem baixa: sem "..", ".", raiz, unidade (':'), '\\',
// segmentos vazios nem caracteres de controle. Vale também para o nome da versão (um segmento).
bool IsSafeRelativePath(std::string_view path);
bool IsSafeVersionName(std::string_view version);

std::string UrlEncodePath(std::string_view path);  // mantém '/' e os caracteres não reservados
std::string HtmlEscape(std::string_view text);

struct ServerUrl {
    std::string hostPort;  // "http://127.0.0.1:47900"
    std::string basePath;  // "" ou "/motor" (sem '/' no fim)
};
// Aceita "http://host[:porta][/base]" e "host[:porta]". https não (a cpp-httplib está sem SSL).
std::optional<ServerUrl> ParseServerUrl(std::string_view url, std::string* error = nullptr);
std::string NormalizeServerUrl(std::string_view url);  // "127.0.0.1:47900/" -> "http://127.0.0.1:47900"

std::string FormatSize(uint64_t bytes);         // "25,3 MB"
std::string FormatDate(std::string_view iso);   // "2026-10-17T17:44:06" -> "17/10/2026"

// --- Pasta de publicações (servidor) ------------------------------------------------------------------------

fs::path ManifestPath(const fs::path& root);
fs::path ReleaseDir(const fs::path& root, std::string_view version);
fs::path HubDir(const fs::path& root);

// Lista os arquivos da pasta (recursivo, em ordem) com tamanho e SHA-256.
std::optional<std::vector<ReleaseFile>> ScanFiles(const fs::path& dir, std::string* error = nullptr);

// Lê a pasta montada pelo motor_package (motor-version.json) e calcula os arquivos.
std::optional<Release> ScanRelease(const fs::path& dir, std::string_view channel, std::string* error = nullptr);

// Manifesto da pasta de publicações (ausente = vazio). Gravação com .tmp e renomear.
std::optional<Manifest> LoadManifest(const fs::path& root, std::string* error = nullptr);
bool SaveManifest(const fs::path& root, const Manifest& manifest, std::string* error = nullptr);

// Refaz o manifesto a partir de versoes/ e hub/: mantém as entradas de pastas que continuam lá (sem recalcular o
// SHA-256 se os tamanhos baterem), calcula as novas e tira as que sumiram.
std::optional<Manifest> RegenerateManifest(const fs::path& root, std::string* error = nullptr);

// Copia a pasta montada para versoes/<versão> (via <versão>.tmp, renomeada no fim) e regenera o manifesto.
// Falha se a versão já estiver publicada.
std::optional<Release> PublishRelease(const fs::path& source, const fs::path& root, std::string_view channel,
                                      std::string* error = nullptr);

// Copia o MotorHub.exe (e o MotorSetup.exe, se não for vazio) para hub/ e grava a versão no manifesto.
std::optional<HubRelease> PublishHub(const fs::path& hubExe, const fs::path& setupExe, std::string_view version,
                                     const fs::path& root, std::string* error = nullptr);

// Tira a versão do manifesto e entrega a pasta para `trash` (a Lixeira, no MotorServidor).
using TrashFn = std::function<bool(const fs::path&, std::string* error)>;
bool UnpublishRelease(const fs::path& root, std::string_view version, const TrashFn& trash,
                      std::string* error = nullptr);

} // namespace distrib
