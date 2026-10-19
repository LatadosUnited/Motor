#pragma once

#include "motor/project/CollisionLayers.h"
#include "motor/project/GameOptions.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace motor {

// Identidade do jogo exportado (Configurações do jogo, Fase 21).
struct GameIdentity {
    std::string executable;        // nome do .exe sem a extensão; vazio = o nome do projeto
    std::string version = "1.0.0";  // vai para as propriedades do .exe (números separados por ponto)
    std::string icon;              // .ico ou .png do projeto, relativo à raiz ("Assets/icone.png"); vazio = sem
    bool operator==(const GameIdentity&) const = default;
};

// Um projeto é uma pasta com um arquivo <Nome>.mproject e a pasta Assets/:
//
//   MeuJogo/
//     MeuJogo.mproject     JSON: nome, versão do formato, versão da engine, cena inicial
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

    bool Save(std::string* error = nullptr) const;  // grava .tmp e renomeia

    const std::string& Name() const { return m_name; }
    const std::filesystem::path& RootDir() const { return m_root; }
    std::filesystem::path ProjectFile() const;
    std::filesystem::path AssetsDir() const { return m_root / kAssetsFolder; }

    // Cena aberta quando o jogo inicia. Caminho absoluto; vazio se não definida.
    std::filesystem::path StartupScene() const;
    void SetStartupScene(const std::filesystem::path& absolutePath);

    // Versão do Motor que gravou o projeto por último ("0.2.0"); vazio = desconhecida (formato 1, de antes da
    // Fase 14). Save grava sempre a versão desta engine (motor::kVersion) e atualiza este valor.
    const std::string& EngineVersion() const { return m_engineVersion; }
    // true se o projeto tem engineVersion válida e maior que motor::kVersion (não deve ser rebaixado sem avisar).
    bool IsNewerThanCurrentEngine() const;

    // Camadas de colisão (nomes e quem se choca com quem), usadas pela física de todas as cenas.
    const CollisionLayers& Layers() const { return m_layers; }
    void SetLayers(const CollisionLayers& layers) { m_layers = layers; }

    const GameIdentity& Identity() const { return m_identity; }
    void SetIdentity(const GameIdentity& identity) { m_identity = identity; }
    // Nome do .exe exportado (sem ".exe"): o da identidade, ou o do projeto se vazio ou inválido.
    std::string ExecutableName() const;

    // Opções com que o jogo exportado abre da primeira vez (antes de o jogador mudar alguma).
    const GameOptions& DefaultOptions() const { return m_options; }
    void SetDefaultOptions(const GameOptions& options) { m_options = options; }

    std::filesystem::path ToAbsolute(std::string_view relative) const;
    std::string ToRelative(const std::filesystem::path& absolute) const;  // vazio se estiver fora do projeto

private:
    std::string m_name;
    std::filesystem::path m_root;
    std::string m_startupScene;  // relativo à raiz
    mutable std::string m_engineVersion;  // Save (const) grava a versão atual
    CollisionLayers m_layers;
    GameOptions m_options;
    GameIdentity m_identity;
};

// "1.2.3" -> {1, 2, 3, 0}: até quatro números (os que faltam são 0); false se tiver algo além de números e pontos.
bool ParseGameVersion(std::string_view version, int (&parts)[4]);

} // namespace motor
