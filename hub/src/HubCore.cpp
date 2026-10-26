#include "HubCore.h"

#include <motor/common/FileWrite.h>
#include <motor/common/FileRead.h>
#include <motor/common/Text.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cwctype>
#include <ctime>
#include <format>
#include <fstream>
#include <random>
#include <sstream>

namespace hub {

using json = nlohmann::ordered_json;

namespace {
using motor::ReadTextFile;
using motor::SetError;

bool IsInside(const fs::path& path, const fs::path& root) {
    const fs::path rel = path.lexically_normal().lexically_relative(root.lexically_normal());
    return !rel.empty() && *rel.begin() != "..";
}

// Minúsculas para a busca: ASCII e, em UTF-8, as maiúsculas do Latin-1 (U+00C0..U+00DE, sem o ×), que ficam
// todas em 0xC3 0x80..0x9E e viram minúsculas somando 0x20 ao segundo byte.
std::string FoldCase(std::string_view text) {
    std::string out(text);
    for (size_t i = 0; i < out.size(); ++i) {
        const auto c = static_cast<unsigned char>(out[i]);
        if (c >= 'A' && c <= 'Z') {
            out[i] = static_cast<char>(c + 32);
        } else if (c == 0xC3 && i + 1 < out.size()) {
            const auto next = static_cast<unsigned char>(out[i + 1]);
            if (next >= 0x80 && next <= 0x9E && next != 0x97) out[i + 1] = static_cast<char>(next + 0x20);
            ++i;
        }
    }
    return out;
}

// Um argumento entre aspas, como o CommandLineToArgvW lê de volta: barras antes de aspas são dobradas.
std::wstring QuoteArgument(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (auto it = arg.begin();; ++it) {
        size_t backslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
        } else {
            out.append(backslashes, L'\\');
        }
        out.push_back(*it);
    }
    out.push_back(L'"');
    return out;
}
} // namespace

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------

bool SamePath(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    std::wstring wa = fs::absolute(a, ec).lexically_normal().wstring();
    std::wstring wb = fs::absolute(b, ec).lexically_normal().wstring();
    // "C:\Jogo\" e "C:\Jogo" são a mesma pasta.
    while (wa.size() > 1 && (wa.back() == L'\\' || wa.back() == L'/')) wa.pop_back();
    while (wb.size() > 1 && (wb.back() == L'\\' || wb.back() == L'/')) wb.pop_back();
#ifdef _WIN32
    const auto lower = [](std::wstring s) {
        std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        std::replace(s.begin(), s.end(), L'/', L'\\');
        return s;
    };
    return lower(wa) == lower(wb);
#else
    return wa == wb;
#endif
}

bool MatchesSearch(std::string_view text, std::string_view query) {
    if (query.empty()) return true;
    return FoldCase(text).find(FoldCase(query)) != std::string::npos;
}

// ---------------------------------------------------------------------------
// Versões
// ---------------------------------------------------------------------------

SemVer ParseVersion(std::string_view text) {
    SemVer v;
    const size_t suffix = text.find_first_of("-+");
    if (suffix != std::string_view::npos) text = text.substr(0, suffix);
    int parts[3] = {0, 0, 0};
    int count = 0;
    const char* p = text.data();
    const char* end = text.data() + text.size();
    while (p < end && count < 3) {
        const auto [next, ec] = std::from_chars(p, end, parts[count]);
        if (ec != std::errc() || parts[count] < 0) return {};
        ++count;
        p = next;
        if (p == end) break;
        if (*p != '.') return {};
        ++p;
        if (p == end) return {};  // "1.2."
    }
    if (p != end || count < 2) return {};
    v.major = parts[0];
    v.minor = parts[1];
    v.patch = parts[2];
    v.valid = true;
    return v;
}

int CompareVersions(std::string_view a, std::string_view b) {
    const SemVer va = ParseVersion(a);
    const SemVer vb = ParseVersion(b);
    if (va.valid != vb.valid) return va.valid ? 1 : -1;
    if (!va.valid) return a.compare(b) < 0 ? -1 : (a == b ? 0 : 1);
    if (va.major != vb.major) return va.major < vb.major ? -1 : 1;
    if (va.minor != vb.minor) return va.minor < vb.minor ? -1 : 1;
    if (va.patch != vb.patch) return va.patch < vb.patch ? -1 : 1;
    return 0;
}

std::optional<InstalledVersion> ReadInstalledVersion(const fs::path& dir, std::string* error) {
    const fs::path file = dir / kVersionFile;
    const auto text = ReadTextFile(file);
    if (!text) {
        SetError(error, std::format("'{}' não é uma instalação do Motor (falta {}).", ToUtf8(dir), kVersionFile));
        return std::nullopt;
    }
    try {
        const json root = json::parse(*text);
        if (!root.is_object() || root.value("format", std::string()) != "motor-version") {
            SetError(error, std::format("'{}' não é um {} do Motor.", ToUtf8(file), kVersionFile));
            return std::nullopt;
        }
        InstalledVersion v;
        v.version = root.value("version", std::string());
        v.scriptApiVersion = root.value("scriptApiVersion", 0);
        v.buildType = root.value("buildType", std::string());
        v.dir = dir;
        v.editor = dir / PathFromUtf8(root.value("editor", std::string(kEditorExe)));
        if (!ParseVersion(v.version).valid) {
            SetError(error, std::format("Versão inválida em '{}': '{}'.", ToUtf8(file), v.version));
            return std::nullopt;
        }
        std::error_code ec;
        if (!fs::is_regular_file(v.editor, ec)) {
            SetError(error, std::format("O editor não está em '{}'.", ToUtf8(dir)));
            return std::nullopt;
        }
        return v;
    } catch (const json::exception& e) {
        SetError(error, std::format("'{}' inválido: {}", ToUtf8(file), e.what()));
        return std::nullopt;
    }
}

std::vector<InstalledVersion> ScanVersions(const fs::path& versionsRoot) {
    std::vector<InstalledVersion> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(versionsRoot, ec)) {
        if (!entry.is_directory(ec) || entry.path().extension() == ".tmp") continue;  // cópia interrompida
        if (auto v = ReadInstalledVersion(entry.path())) out.push_back(std::move(*v));
    }
    std::sort(out.begin(), out.end(), [](const InstalledVersion& a, const InstalledVersion& b) {
        return CompareVersions(a.version, b.version) > 0;
    });
    return out;
}

std::optional<InstalledVersion> InstallVersion(const fs::path& source, const fs::path& versionsRoot, std::string* error) {
    auto version = ReadInstalledVersion(source, error);
    if (!version) return std::nullopt;

    const fs::path dest = versionsRoot / PathFromUtf8(version->version);
    if (SamePath(source, dest)) {
        SetError(error, std::format("A versão {} já está instalada nesta pasta.", version->version));
        return std::nullopt;
    }
    std::error_code ec;
    if (IsInside(fs::absolute(versionsRoot, ec), fs::absolute(source, ec))) {
        SetError(error, "A pasta das versões instaladas não pode ficar dentro da pasta de origem.");
        return std::nullopt;
    }
    if (fs::exists(dest, ec)) {
        SetError(error, std::format("A versão {} já está instalada ({}). Remova-a antes para instalar de novo.",
                                    version->version, ToUtf8(dest)));
        return std::nullopt;
    }

    fs::path tmp = dest;
    tmp += ".tmp";
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    if (!ec) fs::copy(source, tmp, fs::copy_options::recursive, ec);
    if (!ec) motor::ReplaceFileAtomic(tmp, dest, ec);  // espera se o antivírus estiver lendo a cópia
    if (ec) {
        std::error_code ignored;
        fs::remove_all(tmp, ignored);
        SetError(error, std::format("Não foi possível copiar para '{}': {}", ToUtf8(dest), ec.message()));
        return std::nullopt;
    }
    return ReadInstalledVersion(dest, error);
}

const InstalledVersion* FindVersion(const std::vector<InstalledVersion>& installed, std::string_view version) {
    for (const InstalledVersion& v : installed) {
        if (v.version == version) return &v;
    }
    return nullptr;
}

const InstalledVersion* DefaultVersion(const std::vector<InstalledVersion>& installed, std::string_view preferred) {
    if (const InstalledVersion* v = FindVersion(installed, preferred)) return v;
    const InstalledVersion* newest = nullptr;
    for (const InstalledVersion& v : installed) {
        if (!newest || CompareVersions(v.version, newest->version) > 0) newest = &v;
    }
    return newest;
}

// ---------------------------------------------------------------------------
// Projetos
// ---------------------------------------------------------------------------

ProjectInfo ReadProject(const fs::path& fileOrDir) {
    ProjectInfo info;
    std::error_code ec;
    const bool isFile = fileOrDir.extension() == kProjectExtension && !fs::is_directory(fileOrDir, ec);
    info.root = (isFile ? fileOrDir.parent_path() : fileOrDir).lexically_normal();
    info.name = ToUtf8(info.root.filename());

    if (isFile) {
        info.file = fileOrDir;
    } else if (fs::is_directory(info.root, ec)) {
        for (const auto& entry : fs::directory_iterator(info.root, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == kProjectExtension) {
                info.file = entry.path();
                break;
            }
        }
    }
    if (info.file.empty()) {
        info.error = fs::is_directory(info.root, ec) ? std::format("Nenhum arquivo {} na pasta.", kProjectExtension)
                                                     : "A pasta do projeto não existe.";
        return info;
    }

    const auto text = ReadTextFile(info.file);
    if (!text) {
        info.error = std::format("Não foi possível ler '{}'.", ToUtf8(info.file.filename()));
        return info;
    }
    try {
        const json root = json::parse(*text);
        if (!root.is_object() || root.value("format", std::string()) != "motor-project") {
            info.error = std::format("'{}' não é um projeto do Motor.", ToUtf8(info.file.filename()));
            return info;
        }
        info.name = root.value("name", ToUtf8(info.file.stem()));
        info.engineVersion = root.value("engineVersion", std::string());
        info.found = true;
    } catch (const json::exception& e) {
        info.error = std::format("Arquivo de projeto inválido: {}", e.what());
    }
    return info;
}

std::string ValidateProjectName(std::string_view name) {
    if (name.empty()) return "O nome não pode ficar vazio.";
    if (name.size() > 64) return "O nome deve ter no máximo 64 caracteres.";
    if (name.find_first_of("<>:\"/\\|?*") != std::string_view::npos) {
        return "O nome não pode conter < > : \" / \\ | ? *";
    }
    for (unsigned char c : name) {
        if (c < 32) return "O nome contém caracteres de controle.";
    }
    if (name.back() == '.' || name.back() == ' ' || name.front() == ' ') {
        return "O nome não pode começar com espaço nem terminar com espaço ou ponto.";
    }
    return {};
}

std::string CheckNewProject(const fs::path& parent, std::string_view name) {
    if (std::string problem = ValidateProjectName(name); !problem.empty()) return problem;
    std::error_code ec;
    if (parent.empty() || !fs::is_directory(parent, ec)) return std::format("A pasta '{}' não existe.", ToUtf8(parent));
    const fs::path target = parent / PathFromUtf8(name);
    if (fs::exists(target, ec) && !fs::is_empty(target, ec)) {
        return std::format("A pasta '{}' já existe e não está vazia.", ToUtf8(target));
    }
    return {};
}

// ---------------------------------------------------------------------------
// Escolha da versão
// ---------------------------------------------------------------------------

VersionChoice ChooseVersion(std::string_view projectVersion, const std::vector<InstalledVersion>& installed,
                            std::string_view defaultVersion) {
    VersionChoice choice;
    if (installed.empty()) return choice;

    if (projectVersion.empty()) {
        choice.match = VersionMatch::Unknown;
        choice.version = DefaultVersion(installed, defaultVersion);
        return choice;
    }
    if (const InstalledVersion* exact = FindVersion(installed, projectVersion)) {
        choice.match = VersionMatch::Exact;
        choice.version = exact;
        return choice;
    }

    choice.match = VersionMatch::Missing;
    const SemVer wanted = ParseVersion(projectVersion);
    const InstalledVersion* sameMinor = nullptr;  // mais nova com o mesmo maior.menor
    const InstalledVersion* nextNewer = nullptr;  // mais antiga entre as mais novas que o projeto
    for (const InstalledVersion& v : installed) {
        const SemVer sv = ParseVersion(v.version);
        if (wanted.valid && sv.major == wanted.major && sv.minor == wanted.minor &&
            (!sameMinor || CompareVersions(v.version, sameMinor->version) > 0)) {
            sameMinor = &v;
        }
        if (CompareVersions(v.version, projectVersion) > 0 &&
            (!nextNewer || CompareVersions(v.version, nextNewer->version) < 0)) {
            nextNewer = &v;
        }
    }
    choice.version = sameMinor ? sameMinor : nextNewer ? nextNewer : DefaultVersion(installed, defaultVersion);
    choice.downgrade = CompareVersions(choice.version->version, projectVersion) < 0;
    return choice;
}

// ---------------------------------------------------------------------------
// Configuração
// ---------------------------------------------------------------------------

HubConfig HubConfig::Load(const fs::path& file, std::string* warning) {
    HubConfig config;
    const auto text = ReadTextFile(file);
    if (!text) return config;  // primeira execução
    try {
        const json root = json::parse(*text);
        if (!root.is_object() || root.value("format", std::string()) != kFormat) {
            SetError(warning, std::format("'{}' não é uma configuração do hub: ignorada.", ToUtf8(file)));
            return config;
        }
        // Versões mais novas do formato: lê o que conhece (campos novos são ignorados).
        config.defaultVersion = root.value("defaultVersion", std::string());
        config.newProjectDir = PathFromUtf8(root.value("newProjectDir", std::string()));
        if (root.contains("servers") && root["servers"].is_array()) {  // formato 2
            config.servers.clear();
            for (const json& server : root["servers"]) {
                if (server.is_string()) config.AddServer(server.get<std::string>());
            }
        }
        for (const json& p : root.value("projects", json::array())) {
            if (!p.is_object()) continue;
            const std::string path = p.value("path", std::string());
            if (path.empty()) continue;
            HubProject project;
            project.root = PathFromUtf8(path);
            project.lastOpened = p.value("lastOpened", int64_t{0});
            if (!config.FindProject(project.root)) config.projects.push_back(std::move(project));
        }
    } catch (const json::exception& e) {
        SetError(warning, std::format("Configuração do hub inválida ({}): {}", ToUtf8(file), e.what()));
        return HubConfig{};
    }
    return config;
}

bool HubConfig::Save(const fs::path& file, std::string* error) const {
    json root;
    root["format"] = kFormat;
    root["version"] = kVersion;
    root["defaultVersion"] = defaultVersion;
    root["newProjectDir"] = ToUtf8(newProjectDir);
    json list = json::array();
    for (const HubProject& p : projects) list.push_back({{"path", ToUtf8(p.root)}, {"lastOpened", p.lastOpened}});
    root["projects"] = std::move(list);
    root["servers"] = servers;

    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    return motor::WriteFileAtomic(file, root.dump(2, ' ', false, json::error_handler_t::replace) + '\n', error);
}

HubProject* HubConfig::FindProject(const fs::path& root) {
    for (HubProject& p : projects) {
        if (SamePath(p.root, root)) return &p;
    }
    return nullptr;
}

bool HubConfig::AddProject(const fs::path& root) {
    if (FindProject(root)) return false;
    projects.insert(projects.begin(), HubProject{root.lexically_normal(), 0});
    return true;
}

bool HubConfig::RemoveProject(const fs::path& root) {
    const size_t before = projects.size();
    std::erase_if(projects, [&](const HubProject& p) { return SamePath(p.root, root); });
    return projects.size() != before;
}

void HubConfig::MarkOpened(const fs::path& root, int64_t now) {
    HubProject entry{root.lexically_normal(), now};
    if (HubProject* existing = FindProject(root)) entry.root = existing->root;
    RemoveProject(root);
    projects.insert(projects.begin(), std::move(entry));
}

namespace {
std::string NormalizeServer(std::string_view url) {
    std::string s(url);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '/')) s.pop_back();
    if (!s.empty() && s.find("://") == std::string::npos) s = "http://" + s;
    return s;
}
} // namespace

bool HubConfig::AddServer(std::string_view url) {
    const std::string normalized = NormalizeServer(url);
    if (normalized.empty() || std::find(servers.begin(), servers.end(), normalized) != servers.end()) return false;
    servers.push_back(normalized);
    return true;
}

bool HubConfig::RemoveServer(std::string_view url) {
    const size_t before = servers.size();
    std::erase(servers, NormalizeServer(url));
    return servers.size() != before;
}

std::string FormatLastOpened(int64_t lastOpened, int64_t now) {
    if (lastOpened <= 0) return "nunca";
    const int64_t diff = std::max<int64_t>(0, now - lastOpened);
    constexpr int64_t kMinute = 60, kHour = 3600, kDay = 86400;
    if (diff < 2 * kMinute) return "agora há pouco";
    if (diff < kHour) return std::format("há {} min", diff / kMinute);
    if (diff < kDay) return std::format("há {} h", diff / kHour);
    if (diff < 2 * kDay) return "ontem";
    if (diff < 30 * kDay) return std::format("há {} dias", diff / kDay);
    const std::time_t t = static_cast<std::time_t>(lastOpened);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return std::format("{:02}/{:02}/{:04}", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
}

int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// Linha de comando do editor
// ---------------------------------------------------------------------------

std::vector<std::string> OpenProjectArgs(const fs::path& projectRoot) {
    return {"--project", ToUtf8(projectRoot)};
}

std::vector<std::string> CreateProjectArgs(const fs::path& parent, std::string_view name) {
    return {"--create", ToUtf8(parent), std::string(name)};
}

std::wstring BuildCommandLine(const fs::path& exe, const std::vector<std::string>& args) {
    // O primeiro item (o programa) é lido só com aspas, sem escapes: caminhos não têm aspas.
    std::wstring line = L"\"" + exe.wstring() + L"\"";
    for (const std::string& arg : args) {
        line += L' ';
        line += QuoteArgument(PathFromUtf8(arg).wstring());
    }
    return line;
}

// ---------------------------------------------------------------------------
// Hub residente
// ---------------------------------------------------------------------------

namespace {
std::wstring FoldPathText(std::wstring s) {
#ifdef _WIN32
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    std::replace(s.begin(), s.end(), L'/', L'\\');
#endif
    return s;
}
} // namespace

std::string InstanceKey(const fs::path& dataDir) {
    std::error_code ec;
    std::wstring text = fs::absolute(dataDir, ec).lexically_normal().wstring();
    while (text.size() > 1 && (text.back() == L'\\' || text.back() == L'/')) text.pop_back();
    text = FoldPathText(std::move(text));
    // FNV-1a de 64 bits: estável entre execuções e versões (o std::hash não promete isso).
    uint64_t hash = 14695981039346656037ull;
    for (wchar_t c : text) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ull;
    }
    return std::format("{:016x}", hash);
}

bool SameStartupCommand(std::string_view a, std::string_view b) {
    return FoldPathText(PathFromUtf8(std::string(a)).wstring()) == FoldPathText(PathFromUtf8(std::string(b)).wstring());
}

// ---------------------------------------------------------------------------
// MCP do Hub
// ---------------------------------------------------------------------------

std::string LoadOrCreateMcpToken(const fs::path& file, std::string* error) {
    if (const auto text = ReadTextFile(file)) {
        std::string token = *text;
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) token.pop_back();
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) token.erase(0, 1);
        if (!token.empty()) return token;
    }
    // O random_device do MinGW e do MSVC usa o gerador do sistema (rand_s), não uma semente fixa.
    std::random_device random;
    std::string token;
    for (int i = 0; i < 8; ++i) token += std::format("{:08x}", static_cast<uint32_t>(random()));
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (!motor::WriteFileAtomic(file, token + "\n", error)) return {};
    return token;
}

} // namespace hub
