#include "motor/project/Project.h"

#include "motor/common/FileWrite.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/core/Version.h"
#include "motor/project/GameOptions.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

namespace motor {

using json = nlohmann::ordered_json;

namespace {
constexpr const char* kFormatName = "motor-project";
constexpr int kFormatVersion = 4;  // 2: engineVersion (Fase 14). 3: camadas de colisão em "physics" (Fase 18).
                                   // 4: opções padrão do jogador em "player" e identidade em "game" (Fase 21)

void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

// "physics": {"layers": [{"index": 1, "name": "Jogador", "collidesWith": [0, 1, 3]}, ...]}. Só vão para o
// arquivo as camadas com nome ou que não se chocam com alguma outra; as demais ficam no padrão.
json LayersToJson(const CollisionLayers& layers) {
    json list = json::array();
    for (int i = 0; i < CollisionLayers::kCount; ++i) {
        const auto row = static_cast<size_t>(i);
        if (layers.names[row].empty() && layers.masks[row] == CollisionLayers::kAll) continue;
        json with = json::array();
        for (int j = 0; j < CollisionLayers::kCount; ++j) {
            if (layers.Collide(i, j)) with.push_back(j);
        }
        list.push_back({{"index", i}, {"name", layers.names[row]}, {"collidesWith", std::move(with)}});
    }
    return json{{"layers", std::move(list)}};
}

// Leitura tolerante: entradas com índice fora de 0..15 ou campos de outro tipo são ignoradas. Tabela
// assimétrica (arquivo editado à mão): o par só se choca se os dois lados disserem que sim.
CollisionLayers LayersFromJson(const json& physics) {
    CollisionLayers layers;
    const auto list = physics.find("layers");
    if (list == physics.end() || !list->is_array()) return layers;
    for (const json& entry : *list) {
        if (!entry.is_object() || !entry.contains("index") || !entry["index"].is_number_integer()) continue;
        const int i = entry["index"].get<int>();
        if (i < 0 || i >= CollisionLayers::kCount) continue;
        const auto row = static_cast<size_t>(i);
        if (entry.contains("name") && entry["name"].is_string()) layers.names[row] = entry["name"].get<std::string>();
        if (const auto with = entry.find("collidesWith"); with != entry.end() && with->is_array()) {
            uint16_t mask = 0;
            for (const json& j : *with) {
                if (j.is_number_integer() && j.get<int>() >= 0 && j.get<int>() < CollisionLayers::kCount) {
                    mask = static_cast<uint16_t>(mask | (1u << j.get<int>()));
                }
            }
            layers.masks[row] = mask;
        }
    }
    for (int a = 0; a < CollisionLayers::kCount; ++a) {
        for (int b = a; b < CollisionLayers::kCount; ++b) {
            const bool both = ((layers.masks[static_cast<size_t>(a)] >> b) & 1u) &&
                              ((layers.masks[static_cast<size_t>(b)] >> a) & 1u);
            layers.SetCollide(a, b, both);
        }
    }
    return layers;
}
} // namespace

std::string Project::ValidateName(std::string_view name) {
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

std::optional<Project> Project::Create(const std::filesystem::path& parentDir, std::string_view name,
                                       std::string* error) {
    if (std::string problem = ValidateName(name); !problem.empty()) {
        SetError(error, std::move(problem));
        return std::nullopt;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(parentDir, ec)) {
        SetError(error, std::format("A pasta '{}' não existe.", ToUtf8(parentDir)));
        return std::nullopt;
    }

    const std::filesystem::path root = (std::filesystem::absolute(parentDir, ec) / PathFromUtf8(name)).lexically_normal();
    if (std::filesystem::exists(root, ec) && !std::filesystem::is_empty(root, ec)) {
        SetError(error, std::format("A pasta '{}' já existe e não está vazia.", ToUtf8(root)));
        return std::nullopt;
    }

    std::filesystem::create_directories(root / kAssetsFolder / "Scenes", ec);
    if (ec) {
        SetError(error, std::format("Não foi possível criar '{}': {}", ToUtf8(root), ec.message()));
        return std::nullopt;
    }

    Project project;
    project.m_name = std::string(name);
    project.m_root = root;
    if (!project.Save(error)) return std::nullopt;

    log::Info("Projeto criado: {}", ToUtf8(root));
    return project;
}

std::optional<Project> Project::Open(const std::filesystem::path& fileOrDir, std::string* error) {
    std::error_code ec;
    std::filesystem::path file = fileOrDir;

    if (std::filesystem::is_directory(fileOrDir, ec)) {
        file.clear();
        for (const auto& entry : std::filesystem::directory_iterator(fileOrDir, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == kFileExtension) {
                file = entry.path();
                break;
            }
        }
        if (file.empty()) {
            SetError(error, std::format("Nenhum arquivo {} em '{}'.", kFileExtension, ToUtf8(fileOrDir)));
            return std::nullopt;
        }
    }

    std::ifstream in(file, std::ios::binary);
    if (!in) {
        SetError(error, std::format("Não foi possível abrir '{}'.", ToUtf8(file)));
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();

    try {
        const json root = json::parse(ss.str());
        if (!root.is_object() || root.value("format", std::string()) != kFormatName) {
            SetError(error, std::format("'{}' não é um projeto do Motor.", ToUtf8(file.filename())));
            return std::nullopt;
        }
        const int version = root.value("version", 0);
        if (version < 1 || version > kFormatVersion) {
            SetError(error, std::format("Versão de projeto não suportada: {}.", version));
            return std::nullopt;
        }

        Project project;
        project.m_root = std::filesystem::absolute(file.parent_path(), ec).lexically_normal();
        project.m_name = root.value("name", ToUtf8(file.stem()));
        project.m_startupScene = root.value("startupScene", std::string());
        project.m_engineVersion = root.value("engineVersion", std::string());  // formato 1: desconhecida
        if (const auto physics = root.find("physics"); physics != root.end() && physics->is_object()) {
            project.m_layers = LayersFromJson(*physics);  // formatos 1 e 2: camadas padrão
        }
        if (const auto player = root.find("player"); player != root.end() && player->is_object()) {
            GameOptionsFromJson(player->dump(), project.m_options);  // formatos 1 a 3: opções padrão
        }
        if (const auto game = root.find("game"); game != root.end() && game->is_object()) {
            project.m_identity.executable = game->value("executable", std::string());
            project.m_identity.version = game->value("version", project.m_identity.version);
            project.m_identity.icon = game->value("icon", std::string());
        }

        std::filesystem::create_directories(project.AssetsDir(), ec);  // projeto copiado sem Assets/ ainda abre
        log::Info("Projeto aberto: {} ({})", project.m_name, ToUtf8(project.m_root));
        return project;
    } catch (const json::exception& e) {
        SetError(error, std::format("Arquivo de projeto inválido: {}", e.what()));
        return std::nullopt;
    }
}

bool Project::Save(std::string* error) const {
    json root;
    root["format"] = kFormatName;
    root["version"] = kFormatVersion;
    root["name"] = m_name;
    root["engineVersion"] = kVersion;
    root["startupScene"] = m_startupScene;
    root["physics"] = LayersToJson(m_layers);
    json player = json::parse(GameOptionsToJson(m_options));
    player.erase("format");
    player.erase("version");
    root["player"] = std::move(player);
    root["game"] = {{"executable", m_identity.executable}, {"version", m_identity.version}, {"icon", m_identity.icon}};

    // Grava ao lado e troca: uma falha no meio não estraga o projeto.
    const std::string text = root.dump(2, ' ', false, json::error_handler_t::replace) + '\n';
    if (!WriteFileAtomic(ProjectFile(), text, error)) return false;
    m_engineVersion = kVersion;
    return true;
}

std::string Project::ExecutableName() const {
    if (!m_identity.executable.empty() && ValidateName(m_identity.executable).empty()) return m_identity.executable;
    return m_name;
}

bool ParseGameVersion(std::string_view version, int (&parts)[4]) {
    for (int& p : parts) p = 0;
    int index = 0;
    bool digit = false;
    for (char c : version) {
        if (c >= '0' && c <= '9') {
            if (index >= 4) return false;
            parts[index] = std::min(parts[index] * 10 + (c - '0'), 65535);
            digit = true;
        } else if (c == '.' && digit) {
            ++index;
            digit = false;
        } else {
            return false;
        }
    }
    return digit;
}

std::filesystem::path Project::ProjectFile() const {
    return m_root / PathFromUtf8(m_name + kFileExtension);
}

std::filesystem::path Project::StartupScene() const {
    return m_startupScene.empty() ? std::filesystem::path{} : ToAbsolute(m_startupScene);
}

void Project::SetStartupScene(const std::filesystem::path& absolutePath) {
    m_startupScene = ToRelative(absolutePath);
}

bool Project::IsNewerThanCurrentEngine() const {
    return !m_engineVersion.empty() && ParseVersion(m_engineVersion).valid &&
           CompareVersions(m_engineVersion, kVersion) > 0;
}

std::filesystem::path Project::ToAbsolute(std::string_view relative) const {
    return (m_root / PathFromUtf8(relative)).lexically_normal();
}

std::string Project::ToRelative(const std::filesystem::path& absolute) const {
    if (!IsInside(absolute, m_root)) return {};
    return ToGenericUtf8(absolute.lexically_normal().lexically_relative(m_root));
}

} // namespace motor
