// httplib inclui o winsock2.h: precisa vir antes de qualquer windows.h.
#include <httplib.h>

#include "Download.h"

#include "Sha256.h"

#include <HubCore.h>
#include <motor/common/FileWrite.h>

#include <format>
#include <fstream>

namespace distrib {

using hub::PathFromUtf8;
using hub::ToUtf8;

namespace {
void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

uint64_t FileSize(const fs::path& path) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    return ec ? 0 : size;
}
} // namespace

Client::Client(std::string_view serverUrl) {
    m_url = ParseServerUrl(serverUrl, &m_urlError);
    m_display = NormalizeServerUrl(serverUrl);
    if (!m_url) return;
    m_http = std::make_unique<httplib::Client>(m_url->hostPort);
    m_http->set_connection_timeout(3, 0);  // servidor fora do ar: responde rápido na interface
    m_http->set_read_timeout(30, 0);
    m_http->set_keep_alive(true);          // centenas de arquivos pela mesma conexão
    m_http->set_path_encode(false);        // os caminhos já vão codificados (UrlEncodePath)
}

Client::~Client() = default;

std::optional<std::string> Client::GetText(std::string_view path, std::string* error) {
    if (!m_url) {
        SetError(error, m_urlError);
        return std::nullopt;
    }
    const httplib::Result res = m_http->Get(m_url->basePath + std::string(path));
    if (!res) {
        SetError(error, std::format("Sem resposta de {} ({}).", m_display, httplib::to_string(res.error())));
        return std::nullopt;
    }
    if (res->status != 200) {
        SetError(error, std::format("{} respondeu {} para {}.", m_display, res->status, std::string(path)));
        return std::nullopt;
    }
    return res->body;
}

std::optional<Manifest> Client::FetchManifest(std::string* error) {
    const auto text = GetText(kRouteManifest, error);
    if (!text) return std::nullopt;
    return Manifest::Parse(*text, error);
}

std::optional<HubRelease> Client::FetchHubRelease(std::string* error) {
    const auto text = GetText(kRouteHubLatest, error);
    if (!text) return std::nullopt;
    return ParseHubRelease(*text, error);
}

Client::FileResult Client::DownloadFile(std::string_view urlPath, const fs::path& dest, const ReleaseFile& expected,
                                        const std::atomic<bool>* cancel,
                                        const std::function<void(uint64_t)>& onFileBytes) {
    if (!m_url) return {Status::Failed, false, 0, m_urlError};
    const std::string path = m_url->basePath + std::string(urlPath);
    FileResult result = DownloadOnce(path, dest, expected, cancel, onFileBytes);
    // Um pedaço antigo corrompido estraga o arquivo retomado: tenta uma vez do zero.
    if (result.status == Status::Failed && result.resumed) {
        std::error_code ec;
        fs::remove(dest, ec);
        const uint64_t before = result.received;
        result = DownloadOnce(path, dest, expected, cancel, onFileBytes);
        result.received += before;
    }
    return result;
}

Client::FileResult Client::DownloadOnce(const std::string& path, const fs::path& dest, const ReleaseFile& expected,
                                        const std::atomic<bool>* cancel,
                                        const std::function<void(uint64_t)>& onFileBytes) {
    FileResult result;
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);

    uint64_t have = FileSize(dest);
    if (have > expected.size) {
        fs::remove(dest, ec);
        have = 0;
    }
    if (have == expected.size) {
        // Já baixado (download anterior interrompido depois deste arquivo): só confere.
        const auto sha = Sha256File(dest);
        if (sha && *sha == expected.sha256) {
            if (onFileBytes) onFileBytes(have);
            result.status = Status::Ok;
            return result;
        }
        fs::remove(dest, ec);
        have = 0;
    }

    httplib::Headers headers;
    if (have > 0) headers.emplace("Range", std::format("bytes={}-", have));
    std::ofstream out;
    uint64_t written = have;
    int badStatus = 0;
    const httplib::Result res = m_http->Get(
        path, headers,
        [&](const httplib::Response& response) {
            if (response.status == 206 && have > 0) {
                out.open(dest, std::ios::binary | std::ios::app);
                result.resumed = true;
            } else if (response.status == 200) {
                out.open(dest, std::ios::binary | std::ios::trunc);  // servidor sem Range: recomeça
                written = 0;
            } else {
                badStatus = response.status;
                return false;
            }
            if (onFileBytes) onFileBytes(written);
            return static_cast<bool>(out);
        },
        [&](const char* data, size_t size) {
            if (cancel && cancel->load()) return false;
            out.write(data, static_cast<std::streamsize>(size));
            written += size;
            result.received += size;
            if (onFileBytes) onFileBytes(written);
            return static_cast<bool>(out);
        });
    out.close();

    if (cancel && cancel->load()) {
        result.status = Status::Cancelled;
        return result;
    }
    if (badStatus != 0) {
        result.error = std::format("O servidor respondeu {} para {}.", badStatus, path);
        return result;
    }
    if (!res) {
        result.error = std::format("Download de {} interrompido ({}).", path, httplib::to_string(res.error()));
        return result;
    }
    if (FileSize(dest) != expected.size) {
        result.error = std::format("'{}' veio com {} bytes; o manifesto diz {}.", expected.path, FileSize(dest),
                                   expected.size);
        fs::remove(dest, ec);
        return result;
    }
    const auto sha = Sha256File(dest, &result.error);
    if (!sha) return result;
    if (*sha != expected.sha256) {
        result.error = std::format("O SHA-256 de '{}' não confere com o manifesto (arquivo corrompido ou trocado).",
                                   expected.path);
        fs::remove(dest, ec);
        return result;
    }
    result.status = Status::Ok;
    return result;
}

std::string FileUrl(std::string_view version, std::string_view path) {
    return std::string(kRouteFiles) + UrlEncodePath(version) + "/" + UrlEncodePath(path);
}

std::string HubFileUrl(std::string_view name) {
    return std::string(kRouteHubFiles) + UrlEncodePath(name);
}

fs::path StagingRoot(const fs::path& versionsRoot) {
    // Começa com ponto e não tem motor-version.json: o hub não lista como versão instalada.
    return versionsRoot / ".baixando";
}

fs::path StagingDir(const fs::path& versionsRoot, std::string_view version) {
    return StagingRoot(versionsRoot) / PathFromUtf8(version);
}

uint64_t StagedBytes(const fs::path& versionsRoot, const Release& release) {
    const fs::path dir = StagingDir(versionsRoot, release.version);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;
    uint64_t total = 0;
    for (const ReleaseFile& f : release.files) total += std::min(FileSize(dir / PathFromUtf8(f.path)), f.size);
    return total;
}

ReleaseOutcome DownloadRelease(std::string_view serverUrl, const Release& release, const fs::path& versionsRoot,
                               const std::atomic<bool>* cancel,
                               const std::function<void(const ReleaseProgress&)>& progress) {
    ReleaseOutcome outcome;
    if (!IsSafeVersionName(release.version) || !hub::ParseVersion(release.version).valid) {
        outcome.error = std::format("Versão inválida no manifesto: '{}'.", release.version);
        return outcome;
    }
    const fs::path dest = versionsRoot / PathFromUtf8(release.version);
    std::error_code ec;
    if (fs::exists(dest, ec)) {
        outcome.error = std::format("O Motor {} já está instalado ({}).", release.version, ToUtf8(dest));
        return outcome;
    }
    Client client(serverUrl);
    if (!client.Valid()) {
        outcome.error = client.UrlError();
        return outcome;
    }

    const fs::path staging = StagingDir(versionsRoot, release.version);
    fs::create_directories(staging, ec);
    if (ec) {
        outcome.error = std::format("Não foi possível criar '{}': {}", ToUtf8(staging), ec.message());
        return outcome;
    }

    ReleaseProgress p;
    p.filesTotal = release.files.size();
    for (const ReleaseFile& f : release.files) p.total += f.size;
    uint64_t completed = 0;
    for (const ReleaseFile& f : release.files) {
        if (!IsSafeRelativePath(f.path)) {
            outcome.error = std::format("Caminho inseguro no manifesto: '{}'.", f.path);
            return outcome;
        }
        p.currentFile = f.path;
        const Client::FileResult r = client.DownloadFile(
            FileUrl(release.version, f.path), staging / PathFromUtf8(f.path), f, cancel, [&](uint64_t fileBytes) {
                p.done = completed + fileBytes;
                if (progress) progress(p);
            });
        outcome.received += r.received;
        if (r.status != Status::Ok) {
            outcome.status = r.status;
            outcome.error = r.status == Status::Cancelled ? "Download cancelado (dá para retomar depois)." : r.error;
            return outcome;
        }
        outcome.reused += f.size > r.received ? f.size - r.received : 0;  // o que já estava no disco
        completed += f.size;
        ++p.filesDone;
        p.done = completed;
        if (progress) progress(p);
    }

    // Tudo conferido: vira versão instalada de uma vez.
    if (!hub::ReadInstalledVersion(staging, &outcome.error)) return outcome;
    motor::ReplaceFileAtomic(staging, dest, ec);  // espera se o antivírus estiver lendo o que acabou de chegar
    if (ec) {
        outcome.error = std::format("Não foi possível mover '{}' para '{}': {}", ToUtf8(staging), ToUtf8(dest),
                                    ec.message());
        return outcome;
    }
    if (fs::is_empty(StagingRoot(versionsRoot), ec)) fs::remove(StagingRoot(versionsRoot), ec);
    outcome.status = Status::Ok;
    outcome.dir = dest;
    return outcome;
}

} // namespace distrib
