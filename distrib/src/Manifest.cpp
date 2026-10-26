#include "Manifest.h"

#include "Sha256.h"

#include <HubCore.h>
#include <motor/common/FileWrite.h>
#include <motor/common/FileRead.h>
#include <motor/common/Text.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

namespace distrib {

using json = nlohmann::ordered_json;
using hub::PathFromUtf8;
using hub::ToUtf8;

namespace {
using motor::ReadTextFile;
using motor::SetError;

json FilesToJson(const std::vector<ReleaseFile>& files) {
    json list = json::array();
    for (const ReleaseFile& f : files) list.push_back({{"path", f.path}, {"size", f.size}, {"sha256", f.sha256}});
    return list;
}

bool IsHex64(std::string_view s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

std::vector<ReleaseFile> FilesFromJson(const json& list) {
    std::vector<ReleaseFile> out;
    if (!list.is_array()) return out;
    for (const json& f : list) {
        if (!f.is_object()) continue;
        ReleaseFile file;
        file.path = f.value("path", std::string());
        file.size = f.value("size", uint64_t{0});
        file.sha256 = f.value("sha256", std::string());
        // Arquivo com caminho inseguro ou sem hash não entra: quem baixa não teria como conferir.
        if (!IsSafeRelativePath(file.path) || !IsHex64(file.sha256)) continue;
        out.push_back(std::move(file));
    }
    return out;
}

json HubToJson(const HubRelease& h) {
    return {{"version", h.version}, {"date", h.date}, {"files", FilesToJson(h.files)}};
}

std::optional<HubRelease> HubFromJson(const json& j) {
    if (!j.is_object()) return std::nullopt;
    HubRelease h;
    h.version = j.value("version", std::string());
    h.date = j.value("date", std::string());
    h.files = FilesFromJson(j.value("files", json::array()));
    if (!hub::ParseVersion(h.version).valid || !h.Find(kHubExe)) return std::nullopt;
    return h;
}

void SortReleases(std::vector<Release>& releases) {
    std::sort(releases.begin(), releases.end(), [](const Release& a, const Release& b) {
        return hub::CompareVersions(a.version, b.version) > 0;
    });
}

std::string NowIso() {
    const std::time_t t = static_cast<std::time_t>(hub::NowSeconds());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                       tm.tm_min, tm.tm_sec);
}

// Os arquivos no disco têm os mesmos caminhos e tamanhos da lista? (Regenerar sem recalcular o SHA-256.)
bool SameSizes(const fs::path& dir, const std::vector<ReleaseFile>& files) {
    std::error_code ec;
    size_t count = 0;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (it->is_regular_file(ec)) ++count;
    }
    if (ec || count != files.size()) return false;
    for (const ReleaseFile& f : files) {
        const fs::path p = dir / PathFromUtf8(f.path);
        if (!fs::is_regular_file(p, ec) || fs::file_size(p, ec) != f.size) return false;
    }
    return true;
}
} // namespace

// ---------------------------------------------------------------------------
// Manifesto
// ---------------------------------------------------------------------------

const ReleaseFile* HubRelease::Find(std::string_view name) const {
    for (const ReleaseFile& f : files) {
        if (f.path == name) return &f;
    }
    return nullptr;
}

std::optional<Manifest> Manifest::Parse(std::string_view text, std::string* error) {
    try {
        const json root = json::parse(text);
        if (!root.is_object() || root.value("format", std::string()) != kFormat) {
            SetError(error, "A resposta não é um manifesto de versões do Motor.");
            return std::nullopt;
        }
        // Versões mais novas do formato: lê o que conhece.
        Manifest m;
        for (const json& r : root.value("releases", json::array())) {
            if (!r.is_object()) continue;
            Release rel;
            rel.version = r.value("version", std::string());
            if (!IsSafeVersionName(rel.version) || !hub::ParseVersion(rel.version).valid) continue;
            rel.channel = r.value("channel", std::string(kDefaultChannel));
            rel.scriptApiVersion = r.value("scriptApiVersion", 0);
            rel.buildType = r.value("buildType", std::string());
            rel.date = r.value("date", std::string());
            rel.files = FilesFromJson(r.value("files", json::array()));
            for (const ReleaseFile& f : rel.files) rel.size += f.size;
            if (rel.files.empty() || m.Find(rel.version)) continue;
            m.releases.push_back(std::move(rel));
        }
        SortReleases(m.releases);
        if (root.contains("hub")) m.hub = HubFromJson(root["hub"]);
        return m;
    } catch (const json::exception& e) {
        SetError(error, std::format("Manifesto inválido: {}", e.what()));
        return std::nullopt;
    }
}

std::string Manifest::Dump() const {
    json root;
    root["format"] = kFormat;
    root["version"] = kVersion;
    json list = json::array();
    for (const Release& r : releases) {
        list.push_back({{"version", r.version},
                        {"channel", r.channel},
                        {"scriptApiVersion", r.scriptApiVersion},
                        {"buildType", r.buildType},
                        {"date", r.date},
                        {"size", r.size},
                        {"files", FilesToJson(r.files)}});
    }
    root["releases"] = std::move(list);
    if (hub) root["hub"] = HubToJson(*hub);
    return root.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

const Release* Manifest::Find(std::string_view version) const {
    for (const Release& r : releases) {
        if (r.version == version) return &r;
    }
    return nullptr;
}

const Release* Manifest::Latest(std::string_view channel) const {
    for (const Release& r : releases) {  // já em ordem, da mais nova
        if (channel.empty() || r.channel == channel) return &r;
    }
    return nullptr;
}

void Manifest::Put(Release release) {
    Remove(release.version);
    releases.push_back(std::move(release));
    SortReleases(releases);
}

bool Manifest::Remove(std::string_view version) {
    const size_t before = releases.size();
    std::erase_if(releases, [&](const Release& r) { return r.version == version; });
    return releases.size() != before;
}

std::string HubReleaseJson(const HubRelease& h) {
    json root = HubToJson(h);
    root["format"] = "motor-hub-release";
    return root.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

std::optional<HubRelease> ParseHubRelease(std::string_view text, std::string* error) {
    try {
        auto h = HubFromJson(json::parse(text));
        if (!h) SetError(error, "Resposta inválida para o Hub publicado.");
        return h;
    } catch (const json::exception& e) {
        SetError(error, std::format("Resposta inválida para o Hub publicado: {}", e.what()));
        return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// Caminhos e URLs
// ---------------------------------------------------------------------------

bool IsSafeRelativePath(std::string_view path) {
    if (path.empty() || path.size() > 1024) return false;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = std::min(path.find('/', start), path.size());
        const std::string_view seg = path.substr(start, end - start);
        if (seg.empty() || seg == "." || seg == "..") return false;
        for (const char ch : seg) {
            const auto c = static_cast<unsigned char>(ch);
            if (c < 32 || c == 127 || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' ||
                ch == '>' || ch == '|') {
                return false;
            }
        }
        // O Windows ignora ponto e espaço no fim do nome: "a." seria o mesmo arquivo que "a".
        if (seg.back() == '.' || seg.back() == ' ') return false;
        start = end + 1;
        if (end == path.size()) break;
    }
    return true;
}

bool IsSafeVersionName(std::string_view version) {
    return version.find('/') == std::string_view::npos && IsSafeRelativePath(version);
}

std::string UrlEncodePath(std::string_view path) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(path.size() * 3);
    for (const char ch : path) {
        const auto c = static_cast<unsigned char>(ch);
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || ch == '-' ||
                          ch == '_' || ch == '.' || ch == '~' || ch == '/';
        if (keep) {
            out.push_back(ch);
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 15]);
        }
    }
    return out;
}

std::string HtmlEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out.push_back(ch);
        }
    }
    return out;
}

std::optional<ServerUrl> ParseServerUrl(std::string_view url, std::string* error) {
    std::string s(url);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '/')) s.pop_back();
    if (s.empty()) {
        SetError(error, "Endereço do servidor vazio.");
        return std::nullopt;
    }
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    });
    if (lower.starts_with("https://")) {
        SetError(error, "Servidores https ainda não são suportados (use http:// na rede local).");
        return std::nullopt;
    }
    if (lower.starts_with("http://")) {
        s = s.substr(7);
    } else if (s.find("://") != std::string::npos) {
        SetError(error, std::format("Endereço inválido: '{}' (use http://servidor:porta).", std::string(url)));
        return std::nullopt;
    }
    const size_t slash = s.find('/');
    std::string host = s.substr(0, slash);
    std::string base = slash == std::string::npos ? std::string() : s.substr(slash);
    if (host.empty() || host.find_first_of(" \t\"<>\\") != std::string::npos) {
        SetError(error, std::format("Endereço inválido: '{}'.", std::string(url)));
        return std::nullopt;
    }
    if (const size_t colon = host.rfind(':'); colon != std::string::npos && host.find(']') == std::string::npos) {
        const std::string port = host.substr(colon + 1);
        if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), [](char c) {
                return c >= '0' && c <= '9';
            }) || std::stoi(port) == 0 || std::stoi(port) > 65535) {
            SetError(error, std::format("Porta inválida em '{}'.", std::string(url)));
            return std::nullopt;
        }
    } else if (host.find(']') == std::string::npos) {
        host += ":80";
    }
    ServerUrl out;
    out.hostPort = "http://" + host;
    out.basePath = base;
    return out;
}

std::string NormalizeServerUrl(std::string_view url) {
    const auto parsed = ParseServerUrl(url);
    if (!parsed) return std::string(url);
    std::string host = parsed->hostPort;
    if (host.ends_with(":80")) host.resize(host.size() - 3);
    return host + parsed->basePath;
}

std::string FormatSize(uint64_t bytes) {
    const auto fmt = [](double value, const char* unit) {
        std::string s = std::format("{:.1f} {}", value, unit);
        std::replace(s.begin(), s.end(), '.', ',');  // vírgula decimal
        return s;
    };
    if (bytes < 1024) return std::format("{} bytes", bytes);
    if (bytes < 1024 * 1024) return fmt(static_cast<double>(bytes) / 1024.0, "KB");
    if (bytes < 1024ull * 1024 * 1024) return fmt(static_cast<double>(bytes) / (1024.0 * 1024.0), "MB");
    return fmt(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), "GB");
}

std::string FormatDate(std::string_view iso) {
    // AAAA-MM-DD...
    if (iso.size() < 10 || iso[4] != '-' || iso[7] != '-') return std::string(iso);
    return std::format("{}/{}/{}", iso.substr(8, 2), iso.substr(5, 2), iso.substr(0, 4));
}

// ---------------------------------------------------------------------------
// Pasta de publicações
// ---------------------------------------------------------------------------

fs::path ManifestPath(const fs::path& root) { return root / "manifesto.json"; }
fs::path ReleaseDir(const fs::path& root, std::string_view version) { return root / "versoes" / PathFromUtf8(version); }
fs::path HubDir(const fs::path& root) { return root / "hub"; }

std::optional<std::vector<ReleaseFile>> ScanFiles(const fs::path& dir, std::string* error) {
    std::vector<ReleaseFile> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        ReleaseFile f;
        const std::u8string relative = it->path().lexically_relative(dir).generic_u8string();
        f.path.assign(relative.begin(), relative.end());
        if (!IsSafeRelativePath(f.path)) {
            SetError(error, std::format("Nome de arquivo que não dá para publicar: '{}'.", f.path));
            return std::nullopt;
        }
        f.size = it->file_size(ec);
        auto sha = Sha256File(it->path(), error);
        if (!sha) return std::nullopt;
        f.sha256 = std::move(*sha);
        files.push_back(std::move(f));
    }
    if (ec) {
        SetError(error, std::format("Não foi possível ler a pasta '{}': {}", ToUtf8(dir), ec.message()));
        return std::nullopt;
    }
    std::sort(files.begin(), files.end(), [](const ReleaseFile& a, const ReleaseFile& b) { return a.path < b.path; });
    return files;
}

std::optional<Release> ScanRelease(const fs::path& dir, std::string_view channel, std::string* error) {
    const auto installed = hub::ReadInstalledVersion(dir, error);
    if (!installed) return std::nullopt;
    if (!IsSafeVersionName(installed->version)) {
        SetError(error, std::format("Versão com nome inválido: '{}'.", installed->version));
        return std::nullopt;
    }
    Release r;
    r.version = installed->version;
    r.channel = channel.empty() ? std::string(kDefaultChannel) : std::string(channel);
    r.scriptApiVersion = installed->scriptApiVersion;
    r.buildType = installed->buildType;
    try {
        if (const auto text = ReadTextFile(dir / hub::kVersionFile)) r.date = json::parse(*text).value("built", std::string());
    } catch (const json::exception&) {
    }
    if (r.date.empty()) r.date = NowIso();
    auto files = ScanFiles(dir, error);
    if (!files) return std::nullopt;
    r.files = std::move(*files);
    for (const ReleaseFile& f : r.files) r.size += f.size;
    return r;
}

std::optional<Manifest> LoadManifest(const fs::path& root, std::string* error) {
    const auto text = ReadTextFile(ManifestPath(root));
    if (!text) return Manifest{};
    return Manifest::Parse(*text, error);
}

bool SaveManifest(const fs::path& root, const Manifest& manifest, std::string* error) {
    std::error_code ec;
    fs::create_directories(root, ec);
    return motor::WriteFileAtomic(ManifestPath(root), manifest.Dump(), error);
}

std::optional<Manifest> RegenerateManifest(const fs::path& root, std::string* error) {
    std::string loadError;
    Manifest old = LoadManifest(root, &loadError).value_or(Manifest{});
    Manifest fresh;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root / "versoes", ec)) {
        if (!entry.is_directory(ec) || entry.path().extension() == ".tmp") continue;
        const std::string name = ToUtf8(entry.path().filename());
        const auto installed = hub::ReadInstalledVersion(entry.path());
        if (!installed || installed->version != name) continue;  // pasta que não é uma versão publicada
        const Release* known = old.Find(name);
        if (known && SameSizes(entry.path(), known->files)) {
            fresh.releases.push_back(*known);
            continue;
        }
        auto scanned = ScanRelease(entry.path(), known ? known->channel : std::string(kDefaultChannel), error);
        if (!scanned) return std::nullopt;
        fresh.releases.push_back(std::move(*scanned));
    }
    SortReleases(fresh.releases);
    if (old.hub && fs::is_regular_file(HubDir(root) / kHubExe, ec)) {
        HubRelease h = *old.hub;
        std::erase_if(h.files, [&](const ReleaseFile& f) {
            return !fs::is_regular_file(HubDir(root) / PathFromUtf8(f.path), ec) ||
                   fs::file_size(HubDir(root) / PathFromUtf8(f.path), ec) != f.size;
        });
        if (h.Find(kHubExe)) fresh.hub = std::move(h);
    }
    if (!SaveManifest(root, fresh, error)) return std::nullopt;
    return fresh;
}

std::optional<Release> PublishRelease(const fs::path& source, const fs::path& root, std::string_view channel,
                                      std::string* error) {
    auto release = ScanRelease(source, channel, error);
    if (!release) return std::nullopt;

    const fs::path dest = ReleaseDir(root, release->version);
    std::error_code ec;
    if (fs::exists(dest, ec)) {
        SetError(error, std::format("O Motor {} já está publicado ({}). Use --remover antes para publicar de novo.",
                                    release->version, ToUtf8(dest)));
        return std::nullopt;
    }
    if (hub::SamePath(source, dest)) {
        SetError(error, "A origem já é a pasta publicada.");
        return std::nullopt;
    }
    fs::path tmp = dest;
    tmp += ".tmp";
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    if (!ec) fs::copy(source, tmp, fs::copy_options::recursive, ec);
    if (!ec) motor::ReplaceFileAtomic(tmp, dest, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove_all(tmp, ignored);
        SetError(error, std::format("Não foi possível copiar para '{}': {}", ToUtf8(dest), ec.message()));
        return std::nullopt;
    }

    // Regenera (apanha também pastas mexidas à mão) e garante a entrada nova com os hashes já calculados.
    auto manifest = RegenerateManifest(root, error);
    if (!manifest) return std::nullopt;
    manifest->Put(*release);
    if (!SaveManifest(root, *manifest, error)) return std::nullopt;
    return release;
}

std::optional<HubRelease> PublishHub(const fs::path& hubExe, const fs::path& setupExe, std::string_view version,
                                     const fs::path& root, std::string* error) {
    if (!hub::ParseVersion(version).valid) {
        SetError(error, std::format("Versão do Hub inválida: '{}'.", std::string(version)));
        return std::nullopt;
    }
    HubRelease h;
    h.version = std::string(version);
    h.date = NowIso();
    const fs::path dir = HubDir(root);
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (const auto& [source, name] : {std::pair{hubExe, kHubExe}, std::pair{setupExe, kSetupExe}}) {
        if (source.empty()) continue;
        if (!fs::is_regular_file(source, ec)) {
            SetError(error, std::format("'{}' não encontrado.", ToUtf8(source)));
            return std::nullopt;
        }
        const fs::path dest = dir / name;
        fs::path tmp = dest;
        tmp += ".tmp";
        fs::copy_file(source, tmp, fs::copy_options::overwrite_existing, ec);
        if (!ec) motor::ReplaceFileAtomic(tmp, dest, ec);
        if (ec) {
            SetError(error, std::format("Não foi possível copiar '{}': {}", ToUtf8(source), ec.message()));
            return std::nullopt;
        }
        ReleaseFile f;
        f.path = name;
        f.size = fs::file_size(dest, ec);
        auto sha = Sha256File(dest, error);
        if (!sha) return std::nullopt;
        f.sha256 = std::move(*sha);
        h.files.push_back(std::move(f));
    }
    // Instalador de uma publicação anterior que não veio desta vez: sai (seria de outra versão do Hub).
    if (setupExe.empty()) fs::remove(dir / kSetupExe, ec);

    auto manifest = RegenerateManifest(root, error);
    if (!manifest) return std::nullopt;
    manifest->hub = h;
    if (!SaveManifest(root, *manifest, error)) return std::nullopt;
    return h;
}

bool UnpublishRelease(const fs::path& root, std::string_view version, const TrashFn& trash, std::string* error) {
    if (!IsSafeVersionName(version)) {
        SetError(error, std::format("Versão inválida: '{}'.", std::string(version)));
        return false;
    }
    const fs::path dir = ReleaseDir(root, version);
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        SetError(error, std::format("O Motor {} não está publicado.", std::string(version)));
        return false;
    }
    if (!trash(dir, error)) return false;
    return RegenerateManifest(root, error).has_value();
}

} // namespace distrib
