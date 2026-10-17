#include "motor/project/Project.h"

#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/core/Version.h"

#include <nlohmann/json.hpp>

#include <format>
#include <fstream>
#include <sstream>

namespace motor {

using json = nlohmann::ordered_json;

namespace {
constexpr const char* kFormatName = "motor-project";
constexpr int kFormatVersion = 2;  // 2: engineVersion (Fase 14)

void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
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

    // Grava ao lado e renomeia: uma falha no meio não estraga o projeto.
    const std::filesystem::path file = ProjectFile();
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out || !(out << root.dump(2, ' ', false, json::error_handler_t::replace) << '\n') || !out.flush()) {
            SetError(error, std::format("Não foi possível escrever '{}'.", ToUtf8(file)));
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        SetError(error, std::format("Não foi possível escrever '{}'.", ToUtf8(file)));
        return false;
    }
    m_engineVersion = kVersion;
    return true;
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

std::filesystem::path Project::ToAbsolute(std::string_view relative) const {
    return (m_root / PathFromUtf8(relative)).lexically_normal();
}

std::string Project::ToRelative(const std::filesystem::path& absolute) const {
    if (!IsInside(absolute, m_root)) return {};
    return ToGenericUtf8(absolute.lexically_normal().lexically_relative(m_root));
}

} // namespace motor
