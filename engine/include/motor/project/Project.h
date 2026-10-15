#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace motor {

// Um projeto é uma pasta com um arquivo <Nome>.mproject e a pasta Assets/:
//
//   MeuJogo/
//     MeuJogo.mproject     JSON: nome, versão do formato, cena inicial
//     Assets/
//       Scenes/
//
// Caminhos guardados no projeto são relativos à raiz e usam '/'.
class Project {
public:
    static constexpr const char* kFileExtension = ".mproject";
    static constexpr const char* kAssetsFolder = "Assets";

    // Cria <parentDir>/<name>/ com a estrutura padrão. Falha se a pasta já existir e não estiver vazia.
    static std::optional<Project> Create(const std::filesystem::path& parentDir, std::string_view name,
                                         std::string* error = nullptr);

    // Aceita o arquivo .mproject ou a pasta que o contém.
    static std::optional<Project> Open(const std::filesystem::path& fileOrDir, std::string* error = nullptr);

    // Retorna a mensagem de erro, ou vazio se o nome serve como nome de pasta/arquivo (projetos, pastas, cenas).
    static std::string ValidateName(std::string_view name);

    bool Save(std::string* error = nullptr) const;

    const std::string& Name() const { return m_name; }
    const std::filesystem::path& RootDir() const { return m_root; }
    std::filesystem::path ProjectFile() const;
    std::filesystem::path AssetsDir() const { return m_root / kAssetsFolder; }

    // Cena aberta quando o jogo inicia. Caminho absoluto; vazio se não definida.
    std::filesystem::path StartupScene() const;
    void SetStartupScene(const std::filesystem::path& absolutePath);

    std::filesystem::path ToAbsolute(std::string_view relative) const;
    std::string ToRelative(const std::filesystem::path& absolute) const;  // vazio se estiver fora do projeto

private:
    std::string m_name;
    std::filesystem::path m_root;
    std::string m_startupScene;  // relativo à raiz
};

} // namespace motor
