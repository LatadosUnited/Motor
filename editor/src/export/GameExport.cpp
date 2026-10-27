#include "export/GameExport.h"

#include "export/ExeResources.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/assets/AssetLoaders.h>
#include <motor/assets/Model.h>
#include <motor/common/FileRead.h>
#include <motor/common/Text.h>
#include <motor/core/EngineAssets.h>
#include <motor/core/Log.h>
#include <motor/core/Paths.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <deque>
#include <format>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace editor::GameExport {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;
using motor::IsInside;
using motor::LowerExtension;
using motor::PathFromUtf8;
using motor::ReadTextFile;
using motor::ToGenericUtf8;
using motor::ToUtf8;

namespace {

// JSON de um .glb: o primeiro bloco depois do cabeçalho de 12 bytes. Vazio se o arquivo não for um glb.
std::string GlbJsonChunk(const std::string& bytes) {
    auto u32 = [&](size_t at) {
        uint32_t v = 0;
        std::memcpy(&v, bytes.data() + at, 4);
        return v;
    };
    if (bytes.size() < 20 || bytes.compare(0, 4, "glTF") != 0) return {};
    const uint32_t length = u32(12);
    if (u32(16) != 0x4E4F534Au /* "JSON" */ || 20 + static_cast<size_t>(length) > bytes.size()) return {};
    return bytes.substr(20, length);
}

// Coleta com uma fila: cada arquivo novo entra uma vez e as referências dele são seguidas.
class Collector {
public:
    Collector(const motor::Project& project, const motor::AssetDatabase& db) : m_project(project), m_db(db) {}

    void Add(const fs::path& absolute) {
        std::error_code ec;
        if (!IsInside(absolute, m_project.AssetsDir()) || !fs::is_regular_file(absolute, ec)) return;
        if (motor::AssetDatabase::IsMetaFile(absolute)) return;
        const std::string rel = m_project.ToRelative(absolute);
        if (!rel.empty() && m_seen.insert(rel).second) m_queue.push_back(rel);
    }

    Collection Run() {
        while (!m_queue.empty()) {
            const std::string rel = m_queue.front();
            m_queue.pop_front();
            ReferencesOf(rel);
        }
        Collection out;
        out.files.assign(m_seen.begin(), m_seen.end());
        out.missing.assign(m_missing.begin(), m_missing.end());
        return out;
    }

private:
    // Arquivos citados por um asset. Formatos sem referências (imagens, .obj, fontes, sons) não têm caso.
    void ReferencesOf(const std::string& rel) {
        const fs::path file = m_project.ToAbsolute(rel);
        const std::string ext = LowerExtension(file);
        try {
            if (ext == motor::kSceneFileExtension || ext == motor::kPrefabFileExtension ||
                ext == motor::kMaterialFileExtension) {
                WalkRefs(json::parse(ReadTextFile(file).value_or("")), rel);
            } else if (ext == ".gltf") {
                GltfUris(json::parse(ReadTextFile(file).value_or("")), file);
            } else if (ext == ".glb") {
                const std::string chunk = GlbJsonChunk(ReadTextFile(file).value_or(""));
                if (!chunk.empty()) GltfUris(json::parse(chunk), file);
            } else if (ext == ".fbx") {
                FbxTextures(file);
            }
        } catch (const json::exception& e) {
            m_missing.insert(std::format("{}: arquivo inválido ({})", rel, e.what()));
        }
    }

    // Qualquer texto que seja um GUID conhecido (ou "<guid>#<parte>") ou um caminho "Assets/..." que exista.
    // Genérico de propósito: componentes novos (inclusive de scripts) que guardem {guid, path} já entram.
    void WalkRefs(const json& node, const std::string& from, const std::string& key = {}) {
        if (node.is_object()) {
            for (auto it = node.begin(); it != node.end(); ++it) WalkRefs(it.value(), from, it.key());
        } else if (node.is_array()) {
            for (const json& item : node) WalkRefs(item, from, key);
        } else if (node.is_string()) {
            const std::string& text = node.get_ref<const std::string&>();
            const std::string base = text.substr(0, text.find(motor::AssetLibrary::kSubAssetSeparator));
            if (motor::AssetDatabase::IsValidGuid(base)) {
                if (const fs::path path = m_db.PathFor(base); !path.empty()) {
                    Add(path);
                } else if (key == "guid") {
                    m_missing.insert(std::format("{}: GUID {} sem arquivo", from, base));
                }
            } else if (base.starts_with("Assets/")) {
                Add(m_project.ToAbsolute(base));  // caminho de reserva; se não existir, o GUID decide
            }
        }
    }

    // glTF: buffers e imagens em arquivos separados (uri relativa ao .gltf; "data:" é embutido).
    void GltfUris(const json& gltf, const fs::path& file) {
        for (const char* list : {"buffers", "images"}) {
            if (!gltf.contains(list) || !gltf[list].is_array()) continue;
            for (const json& item : gltf[list]) {
                const std::string uri = item.value("uri", std::string());
                if (uri.empty() || uri.starts_with("data:")) continue;
                const fs::path target = (file.parent_path() / PathFromUtf8(Unescape(uri))).lexically_normal();
                std::error_code ec;
                if (fs::is_regular_file(target, ec)) {
                    Add(target);
                } else {
                    m_missing.insert(std::format("{}: arquivo citado não existe ({})", m_project.ToRelative(file), uri));
                }
            }
        }
    }

    // FBX: texturas externas (as embutidas vão dentro do .fbx). As que o leitor não achou já não aparecem no jogo.
    void FbxTextures(const fs::path& file) {
        motor::ModelData data;
        std::string error;
        if (!motor::LoadFbx(file, data, &error)) {
            m_missing.insert(std::format("{}: {}", m_project.ToRelative(file), error));
            return;
        }
        for (const motor::ModelImage& image : data.images) {
            if (!image.file.empty()) Add(image.file);
        }
    }

    static std::string Unescape(const std::string& uri) {  // %20 e companhia
        std::string out;
        for (size_t i = 0; i < uri.size(); ++i) {
            if (uri[i] == '%' && i + 2 < uri.size() && std::isxdigit(static_cast<unsigned char>(uri[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(uri[i + 2]))) {
                out += static_cast<char>(std::stoi(uri.substr(i + 1, 2), nullptr, 16));
                i += 2;
            } else {
                out += uri[i];
            }
        }
        return out;
    }

    const motor::Project& m_project;
    const motor::AssetDatabase& m_db;
    std::set<std::string> m_seen;
    std::deque<std::string> m_queue;
    std::set<std::string> m_missing;
};

void SetError(Result& result, std::string message) {
    result.ok = false;
    result.error = std::move(message);
}

bool CopyFile(const fs::path& from, const fs::path& to, Result& result) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        SetError(result, std::format("Não foi possível copiar '{}': {}", ToUtf8(from), ec.message()));
        return false;
    }
    result.bytes += fs::file_size(to, ec);
    return true;
}

bool CopyTree(const fs::path& from, const fs::path& to, Result& result) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(from, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (!CopyFile(it->path(), to / it->path().lexically_relative(from), result)) return false;
    }
    if (ec) {
        SetError(result, std::format("Não foi possível ler '{}': {}", ToUtf8(from), ec.message()));
        return false;
    }
    return true;
}

// Entradas de primeiro nível criadas pela exportação anterior (lidas da marca).
std::vector<std::string> PreviousEntries(const fs::path& marker) {
    try {
        const json root = json::parse(ReadTextFile(marker).value_or(""));
        if (root.value("format", std::string()) != "motor-export") return {};
        return root.value("entries", std::vector<std::string>{});
    } catch (const json::exception&) {
        return {};
    }
}

// Prepara a pasta de saída: vazia, nova, ou uma exportação anterior (apaga só o que ela tinha criado).
bool PrepareOutput(const fs::path& out, Result& result) {
    std::error_code ec;
    if (fs::exists(out, ec) && !fs::is_directory(out, ec)) {
        SetError(result, std::format("'{}' é um arquivo, não uma pasta.", ToUtf8(out)));
        return false;
    }
    if (fs::is_directory(out, ec) && !fs::is_empty(out, ec)) {
        const fs::path marker = out / kMarkerFile;
        if (!fs::is_regular_file(marker, ec)) {
            SetError(result, std::format("A pasta '{}' não está vazia e não é uma exportação do Motor. "
                                         "Escolha uma pasta vazia.", ToUtf8(out)));
            return false;
        }
        for (const std::string& entry : PreviousEntries(marker)) {
            const fs::path target = (out / PathFromUtf8(entry)).lexically_normal();
            if (!IsInside(target, out) || target == out.lexically_normal()) continue;  // marca adulterada
            fs::remove_all(target, ec);
            if (ec) {
                SetError(result, std::format("Não foi possível limpar '{}' (o jogo está aberto?): {}", ToUtf8(target),
                                             ec.message()));
                return false;
            }
        }
        fs::remove(marker, ec);
    }
    fs::create_directories(out, ec);
    if (ec) {
        SetError(result, std::format("Não foi possível criar '{}': {}", ToUtf8(out), ec.message()));
        return false;
    }
    return true;
}

} // namespace

Collection CollectAssets(const motor::Project& project, const motor::AssetDatabase& db) {
    Collector collector(project, db);
    std::error_code ec;

    // Todas as cenas e a pasta Recursos.
    const fs::path resources = project.ToAbsolute(kResourcesFolder);
    for (auto it = fs::recursive_directory_iterator(project.AssetsDir(), ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (motor::AssetTypeFromPath(it->path()) == motor::AssetType::Scene || IsInside(it->path(), resources)) {
            collector.Add(it->path());
        }
    }

    // "Assets/..." entre aspas nos fontes dos scripts.
    const fs::path scripts = project.RootDir() / "Scripts";
    static const std::regex literal(R"re("(Assets/[^"\\\r\n]+)")re");
    for (auto it = fs::recursive_directory_iterator(scripts, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string source = ReadTextFile(it->path()).value_or("");
        for (auto m = std::sregex_iterator(source.begin(), source.end(), literal); m != std::sregex_iterator(); ++m) {
            collector.Add(project.ToAbsolute((*m)[1].str()));
        }
    }

    return collector.Run();
}

fs::path DefaultOutputDir(const motor::Project& project) {
    return project.RootDir() / "Exportado";
}

namespace {

// Conferências antes de mexer em qualquer arquivo. `out` recebe a pasta de saída absoluta.
bool CheckOptions(const motor::Project& project, const Options& options, const std::string& startup, fs::path& out,
                  Result& result) {
    std::error_code ec;
    if (!fs::is_regular_file(project.ToAbsolute(startup), ec)) {
        SetError(result, std::format("A cena inicial '{}' não existe.", startup));
        return false;
    }
    if (options.outputDir.empty()) {
        SetError(result, "Escolha a pasta de saída.");
        return false;
    }
    out = fs::absolute(options.outputDir, ec).lexically_normal();
    if (IsInside(out, project.AssetsDir())) {
        SetError(result, "A pasta de saída não pode ficar dentro de Assets/ (ela seria lida como assets do projeto).");
        return false;
    }
    if (IsInside(project.RootDir(), out)) {
        SetError(result, "A pasta de saída não pode conter o projeto.");
        return false;
    }
    if (!fs::is_regular_file(options.playerExe, ec)) {
        SetError(result, std::format("Player não encontrado: '{}'. Compile o alvo Player.", ToUtf8(options.playerExe)));
        return false;
    }
    for (const char* dll : kRuntimeDlls) {
        if (!fs::is_regular_file(options.runtimeDir / dll, ec)) {
            SetError(result, std::format("'{}' não encontrada em '{}'.", dll, ToUtf8(options.runtimeDir)));
            return false;
        }
    }
    if (!fs::is_directory(options.engineAssetsDir / "shaders", ec)) {
        SetError(result, std::format("Shaders da engine não encontrados em '{}'.", ToUtf8(options.engineAssetsDir)));
        return false;
    }
    if (!options.scriptsDll.empty() && !fs::is_regular_file(options.scriptsDll, ec)) {
        SetError(result, "A DLL de scripts não foi compilada. Compile os scripts antes de exportar.");
        return false;
    }
    return true;
}

// Player, DLLs, shaders e fontes da engine, os assets usados (com os .meta) e a DLL de scripts.
bool CopyGameFiles(const motor::Project& project, const Options& options, const Collection& assets,
                   const std::vector<ExeResources::IconImage>& icon, const fs::path& out, const std::string& exeName,
                   Result& result) {
    std::error_code ec;
    const fs::path data = out / kDataFolder;
    if (!CopyFile(options.playerExe, out / PathFromUtf8(exeName), result)) return false;
    if (options.exeResources) {
        std::string resourceError;
        const ExeResources::VersionInfo version{project.Name(), project.Identity().version, exeName};
        if (!ExeResources::Apply(out / PathFromUtf8(exeName), icon, version, resourceError)) {
            result.warnings.push_back("o jogo foi exportado sem o ícone e a versão: " + resourceError);
        }
    }
    for (const char* dll : kRuntimeDlls) {
        if (!CopyFile(options.runtimeDir / dll, out / dll, result)) return false;
    }
    for (const char* folder : {"shaders", "fonts"}) {
        if (!CopyTree(options.engineAssetsDir / folder, out / motor::kEngineAssetsFolder / folder, result)) return false;
    }
    for (const std::string& rel : assets.files) {
        const fs::path from = project.ToAbsolute(rel);
        if (!CopyFile(from, data / PathFromUtf8(rel), result)) return false;
        const fs::path meta = motor::AssetDatabase::MetaPathFor(from);
        if (fs::is_regular_file(meta, ec) &&
            !CopyFile(meta, data / PathFromUtf8(rel).parent_path() / meta.filename(), result)) {
            return false;
        }
        // A pasta dos blocos do terreno (e outros companheiros do asset) vai junto.
        for (const fs::path& companion : motor::AssetCompanions(from)) {
            if (!CopyTree(companion, data / PathFromUtf8(rel).parent_path() / companion.filename(), result)) return false;
        }
    }
    if (!options.scriptsDll.empty() && !CopyFile(options.scriptsDll, data / kScriptsDllName, result)) return false;
    return true;
}

// Projeto do jogo: o mesmo .mproject, com a cena inicial escolhida.
bool WriteGameProject(const motor::Project& project, const fs::path& data, const std::string& startup,
                      Result& result) {
    if (!CopyFile(project.ProjectFile(), data / project.ProjectFile().filename(), result)) return false;
    std::string error;
    auto exported = motor::Project::Open(data, &error);
    if (!exported) {
        SetError(result, error);
        return false;
    }
    exported->SetStartupScene(exported->ToAbsolute(startup));
    if (!exported->Save(&error)) {
        SetError(result, error);
        return false;
    }
    return true;
}

// Marca: o que esta exportação criou (para a próxima limpar só isso).
void WriteMarker(const motor::Project& project, const fs::path& out, const std::string& exeName,
                 const std::string& startup) {
    json entries = json::array({exeName, kDataFolder, motor::kEngineAssetsFolder});
    for (const char* dll : kRuntimeDlls) entries.push_back(dll);
    json marker;
    marker["format"] = "motor-export";
    marker["version"] = 1;
    marker["game"] = project.Name();
    marker["gameVersion"] = project.Identity().version;
    marker["startupScene"] = startup;
    marker["entries"] = std::move(entries);
    std::ofstream(out / kMarkerFile, std::ios::binary) << marker.dump(2, ' ', false, json::error_handler_t::replace) << '\n';
}

} // namespace

Result Export(const motor::Project& project, const motor::AssetDatabase& db, const Options& options) {
    Result result;

    // --- Conferências (CheckOptions) ---
    const std::string startup = !options.startupScene.empty() ? options.startupScene
                                                              : project.ToRelative(project.StartupScene());
    if (startup.empty()) {
        SetError(result, "Escolha a cena inicial do jogo.");
        return result;
    }
    fs::path out;
    if (!CheckOptions(project, options, startup, out, result)) return result;

    // Ícone do jogo: lido antes de mexer na pasta, para um arquivo errado não deixar a exportação pela metade.
    std::vector<ExeResources::IconImage> icon;
    if (const std::string& iconFile = project.Identity().icon; !iconFile.empty()) {
        std::string iconError;
        if (!ExeResources::ReadIcon(project.ToAbsolute(iconFile), icon, iconError)) {
            SetError(result, std::format("Ícone do jogo ({}): {}. Troque em Arquivo > Configurações do jogo.",
                                         iconFile, iconError));
            return result;
        }
    }

    const Collection assets = CollectAssets(project, db);
    if (!PrepareOutput(out, result)) return result;

    // --- Cópia ---
    const std::string exeName = project.ExecutableName() + ".exe";
    if (!CopyGameFiles(project, options, assets, icon, out, exeName, result)) return result;
    if (!WriteGameProject(project, out / kDataFolder, startup, result)) return result;
    WriteMarker(project, out, exeName, startup);

    result.ok = true;
    result.executable = out / PathFromUtf8(exeName);
    result.assets = assets.files;
    result.warnings.insert(result.warnings.begin(), assets.missing.begin(), assets.missing.end());
    for (const std::string& w : result.warnings) motor::log::Warn("Exportar: {}", w);
    motor::log::Info("Jogo exportado em {}: {} asset(s), {:.1f} MB", ToUtf8(out), result.assets.size(),
                     static_cast<double>(result.bytes) / (1024.0 * 1024.0));
    return result;
}

} // namespace editor::GameExport
