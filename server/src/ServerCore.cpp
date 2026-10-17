// httplib inclui o winsock2.h: precisa vir antes de qualquer windows.h.
#include <httplib.h>

#include "ServerCore.h"

#include <HubCore.h>

#include <format>
#include <fstream>

namespace distrib {

namespace fs = std::filesystem;
using hub::PathFromUtf8;
using hub::ToUtf8;

namespace {
constexpr const char* kJson = "application/json; charset=utf-8";

// Entrega `file` (tamanho conhecido) aos pedaços; a cpp-httplib aplica o Range pedindo só o trecho.
void ServeFile(httplib::Response& res, const fs::path& file, uint64_t size, const char* contentType) {
    auto in = std::make_shared<std::ifstream>(file, std::ios::binary);
    if (!*in) {
        res.status = 404;
        return;
    }
    res.set_content_provider(
        static_cast<size_t>(size), contentType,
        [in](size_t offset, size_t length, httplib::DataSink& sink) {
            char buffer[64 * 1024];
            in->clear();
            in->seekg(static_cast<std::streamoff>(offset));
            const size_t want = std::min(length, sizeof(buffer));
            in->read(buffer, static_cast<std::streamsize>(want));
            const std::streamsize got = in->gcount();
            if (got <= 0) return false;
            return sink.write(buffer, static_cast<size_t>(got));
        });
}
} // namespace

DistribServer::DistribServer(fs::path root) : m_root(std::move(root)) {}

DistribServer::~DistribServer() { Stop(); }

void DistribServer::Log(const std::string& line) {
    if (log) log(line);
}

std::shared_ptr<const Manifest> DistribServer::CurrentManifest() {
    std::lock_guard lock(m_mutex);
    std::error_code ec;
    // Data e tamanho: duas gravações no mesmo instante (ou com a data copiada) ainda mudam o tamanho.
    const auto time = fs::last_write_time(ManifestPath(m_root), ec);
    const uint64_t size = ec ? 0 : fs::file_size(ManifestPath(m_root), ec);
    if (!m_manifest || (!ec && (time != m_manifestTime || size != m_manifestSize))) {
        std::string error;
        auto loaded = LoadManifest(m_root, &error);
        if (!loaded) {
            Log("Manifesto inválido: " + error);
            if (!m_manifest) m_manifest = std::make_shared<Manifest>();
        } else {
            m_manifest = std::make_shared<Manifest>(std::move(*loaded));
            m_manifestTime = ec ? fs::file_time_type{} : time;
            m_manifestSize = ec ? 0 : size;
        }
    }
    return m_manifest;
}

int DistribServer::Start(const std::string& host, int port, std::string* error) {
    Stop();
    m_http = std::make_unique<httplib::Server>();
    httplib::Server& http = *m_http;

    http.Get("/", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_content(RenderIndexPage(*CurrentManifest(), req.get_header_value("Host")), "text/html; charset=utf-8");
    });
    http.Get(kRouteManifest, [this](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-cache");
        res.set_content(CurrentManifest()->Dump(), kJson);
    });
    http.Get(kRouteHubLatest, [this](const httplib::Request&, httplib::Response& res) {
        const auto manifest = CurrentManifest();
        if (!manifest->hub) {
            res.status = 404;
            res.set_content(R"({"error": "Nenhum Hub publicado neste servidor."})", kJson);
            return;
        }
        res.set_header("Cache-Control", "no-cache");
        res.set_content(HubReleaseJson(*manifest->hub), kJson);
    });
    http.Get(R"(/hub/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        const auto manifest = CurrentManifest();
        const ReleaseFile* file = manifest->hub ? manifest->hub->Find(req.matches[1].str()) : nullptr;
        if (!file) {
            res.status = 404;
            return;
        }
        res.set_header("Content-Disposition", std::format("attachment; filename=\"{}\"", file->path));
        ServeFile(res, HubDir(m_root) / PathFromUtf8(file->path), file->size, "application/octet-stream");
    });
    http.Get(R"(/arquivos/([^/]+)/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        // req.path já vem decodificado (%C3%A7 -> ç): os nomes são comparados em UTF-8.
        const std::string version = req.matches[1].str();
        const std::string path = req.matches[2].str();
        if (!IsSafeVersionName(version) || !IsSafeRelativePath(path)) {
            res.status = 400;
            return;
        }
        const auto manifest = CurrentManifest();
        const Release* release = manifest->Find(version);
        const ReleaseFile* file = nullptr;
        if (release) {
            for (const ReleaseFile& f : release->files) {
                if (f.path == path) {
                    file = &f;
                    break;
                }
            }
        }
        if (!file) {
            res.status = 404;
            return;
        }
        ServeFile(res, ReleaseDir(m_root, version) / PathFromUtf8(path), file->size, "application/octet-stream");
    });

    http.set_logger([this](const httplib::Request& req, const httplib::Response& res) {
        // Arquivos de versão são centenas por download: só os erros entram no log.
        if (req.path.starts_with(kRouteFiles) && res.status < 400) return;
        Log(std::format("{} {} {} {}", req.remote_addr, req.method, req.path, res.status));
    });

    if (port == 0) {
        m_port = http.bind_to_any_port(host);
    } else {
        m_port = http.bind_to_port(host, port) ? port : -1;
    }
    if (m_port <= 0) {
        if (error) {
            *error = std::format("Não foi possível escutar em {}:{} (porta em uso ou sem permissão).", host, port);
        }
        m_http.reset();
        m_port = -1;
        return -1;
    }
    m_thread = std::thread([this] { m_http->listen_after_bind(); });
    m_http->wait_until_ready();
    return m_port;
}

void DistribServer::Stop() {
    if (m_http) m_http->stop();
    if (m_thread.joinable()) m_thread.join();
    m_http.reset();
    m_port = -1;
}

std::string RenderIndexPage(const Manifest& manifest, const std::string& host) {
    const std::string server = "http://" + (host.empty() ? std::string("<este computador>:47900") : host);
    std::string html = R"(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Servidor de versões do Motor</title>
<style>
body{font-family:Segoe UI,system-ui,sans-serif;background:#1c1e21;color:#e4e6ea;margin:0;padding:24px}
main{max-width:920px;margin:0 auto}h1{font-size:26px;margin:0 0 4px}p.dim,td.dim{color:#9aa0a8}
a{color:#7ab4ff}table{border-collapse:collapse;width:100%;margin-top:8px}
th,td{text-align:left;padding:8px 10px;border-bottom:1px solid #33373d}th{color:#9aa0a8;font-weight:600}
.btn{display:inline-block;background:#3468b3;color:#fff;text-decoration:none;padding:10px 18px;border-radius:4px;font-weight:600}
code{background:#2a2d32;padding:2px 6px;border-radius:3px}
</style></head><body><main>
<h1>Servidor de versões do Motor</h1>
)";
    if (manifest.hub && manifest.hub->Find(kSetupExe)) {
        const ReleaseFile* setup = manifest.hub->Find(kSetupExe);
        html += std::format(R"(<p><a class="btn" href="/hub/{}">Baixar o instalador do Hub {}</a> <span class="dim">({})</span></p>
)",
                            kSetupExe, HtmlEscape(manifest.hub->version), FormatSize(setup->size));
    } else if (manifest.hub) {
        html += std::format(R"(<p><a href="/hub/{}">Baixar o Hub {}</a> (instalador não publicado)</p>
)",
                            kHubExe, HtmlEscape(manifest.hub->version));
    } else {
        html += "<p class=\"dim\">Nenhum Hub publicado ainda (<code>MotorServidor --publicar-hub</code>).</p>\n";
    }
    html += std::format(R"(<p class="dim">No Hub, aba Versões, em Servidores: <code>{}</code></p>
<h2>Versões</h2>
)",
                        HtmlEscape(server));
    if (manifest.releases.empty()) {
        html += "<p class=\"dim\">Nenhuma versão publicada (<code>MotorServidor --publicar &lt;pasta&gt;</code>).</p>\n";
    } else {
        html += "<table><tr><th>Versão</th><th>Canal</th><th>API de scripts</th><th>Compilação</th><th>Data</th>"
                "<th>Tamanho</th><th>Arquivos</th></tr>\n";
        for (const Release& r : manifest.releases) {
            html += std::format("<tr><td>Motor {}</td><td>{}</td><td>{}</td><td>{}</td><td>{}</td><td>{}</td>"
                                "<td class=\"dim\">{}</td></tr>\n",
                                HtmlEscape(r.version), HtmlEscape(r.channel), r.scriptApiVersion,
                                HtmlEscape(r.buildType), HtmlEscape(FormatDate(r.date)), FormatSize(r.size),
                                r.files.size());
        }
        html += "</table>\n";
    }
    html += "<p class=\"dim\">Manifesto: <a href=\"/api/versoes\">/api/versoes</a></p>\n</main></body></html>\n";
    return html;
}

} // namespace distrib
