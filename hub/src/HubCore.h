#pragma once

// Núcleo do Hub do Motor (Fase 14), sem janela: versões instaladas, lista de projetos, escolha da versão para
// abrir um projeto, configuração (hub.json) e a linha de comando do editor. Testado em tests/test_hub.cpp.
//
// O hub não liga na libMotor.dll nem inclui headers da engine: ele precisa abrir projetos de qualquer versão,
// inclusive de versões futuras. Fala com o editor só pelo que é estável entre versões:
//   - <versão>/motor-version.json   {"version": "0.2.0", "scriptApiVersion": 10, ...} (alvo motor_package)
//   - <versão>/mcp-ferramentas.json as ferramentas do MCP do editor (a partir do Hub residente)
//   - <projeto>/<Nome>.mproject     {"name", "engineVersion"} (formato 2; o 1 não tem engineVersion)
//   - MotorEditor.exe --project <pasta> | --create <pasta-pai> <nome>
// Todos os caminhos e textos são UTF-8 ("Jogo Ação").

#include <motor/common/Utf8.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hub {

namespace fs = std::filesystem;

// --- UTF-8 (motor/common/Utf8.h, o mesmo da engine: está no common/, que o hub pode usar) ------------------

using motor::PathFromUtf8;
using motor::ToUtf8;
bool SamePath(const fs::path& a, const fs::path& b);  // normalizados; sem diferenciar maiúsculas no Windows

// Busca sem diferenciar maiúsculas (ASCII e as letras acentuadas do Latin-1: "AÇÃO" acha "ação").
bool MatchesSearch(std::string_view text, std::string_view query);

// --- Versões -----------------------------------------------------------------------------------------------

struct SemVer {
    int major = 0, minor = 0, patch = 0;
    bool valid = false;
};
SemVer ParseVersion(std::string_view text);  // "0.2.0"; "1.2" = 1.2.0; sufixo depois de '-' ou '+' é ignorado
// < 0, 0 ou > 0. Versões inválidas são menores que qualquer válida (e iguais entre si pelo texto).
int CompareVersions(std::string_view a, std::string_view b);

inline constexpr const char* kVersionFile = "motor-version.json";
inline constexpr const char* kEditorExe = "MotorEditor.exe";

// Uma versão do Motor instalada (pasta montada pelo alvo motor_package).
struct InstalledVersion {
    std::string version;
    int scriptApiVersion = 0;
    std::string buildType;
    fs::path dir;
    fs::path editor;  // <dir>/MotorEditor.exe (ou o nome em "editor" no motor-version.json)
};

// Lê <dir>/motor-version.json. Falha se não existir, for inválido ou não houver o editor na pasta.
std::optional<InstalledVersion> ReadInstalledVersion(const fs::path& dir, std::string* error = nullptr);

// Ferramentas do MCP do editor de uma versão (Hub residente), gravadas pelo motor_package com
// MotorEditor --mcp-ferramentas: {"format": "motor-mcp-tools", "version": 1, "initialize": {...}, "tools": [...]}.
// O Hub responde com elas o initialize e o tools/list de /mcpengine/<Projeto>, mesmo com o editor fechado.
inline constexpr const char* kMcpToolsFile = "mcp-ferramentas.json";
struct EditorTools {
    std::string engineVersion;
    nlohmann::ordered_json initialize;  // o resultado do initialize do editor (serverInfo, instructions...)
    nlohmann::ordered_json tools;       // a lista do tools/list
};
// Lê <dir>/mcp-ferramentas.json. Versões anteriores ao Hub residente não têm o arquivo: nullopt e `error`.
std::optional<EditorTools> ReadEditorTools(const fs::path& versionDir, std::string* error = nullptr);

// Subpastas de `versionsRoot` que são versões instaladas, da mais nova para a mais antiga.
std::vector<InstalledVersion> ScanVersions(const fs::path& versionsRoot);

// Copia uma pasta montada (com motor-version.json) para <versionsRoot>/<versão>/. Falha se a versão já estiver
// instalada ou se a origem já for essa pasta. Copia para <versão>.tmp e renomeia no fim (cópia interrompida
// não vira versão instalada).
std::optional<InstalledVersion> InstallVersion(const fs::path& source, const fs::path& versionsRoot,
                                               std::string* error = nullptr);

// A versão marcada como padrão, se instalada; senão a mais nova; nullptr sem versões.
const InstalledVersion* DefaultVersion(const std::vector<InstalledVersion>& installed, std::string_view preferred);
const InstalledVersion* FindVersion(const std::vector<InstalledVersion>& installed, std::string_view version);

// --- Projetos ----------------------------------------------------------------------------------------------

inline constexpr const char* kProjectExtension = ".mproject";

struct ProjectInfo {
    fs::path root;
    fs::path file;              // <root>/<Nome>.mproject
    std::string name;           // do .mproject (senão o nome da pasta)
    std::string engineVersion;  // vazio = desconhecida (formato 1)
    bool found = false;         // a pasta e o .mproject existem e são válidos
    std::string error;          // por que não foi encontrado
};

// Aceita a pasta do projeto ou o arquivo .mproject.
ProjectInfo ReadProject(const fs::path& fileOrDir);

// Mesmas regras de motor::Project::ValidateName: vazio = serve como nome de projeto (e de pasta).
std::string ValidateProjectName(std::string_view name);
// Confere antes de chamar o editor com --create: nome, pasta-pai existente e destino vazio ou inexistente.
std::string CheckNewProject(const fs::path& parent, std::string_view name);

// --- Escolha da versão para abrir um projeto ---------------------------------------------------------------

enum class VersionMatch {
    Exact,       // a versão do projeto está instalada: abre direto
    Unknown,     // projeto sem engineVersion (formato 1): pergunta, sugerindo a padrão
    Missing,     // a versão do projeto não está instalada: pergunta, sugerindo a mais próxima
    NoVersions,  // nenhuma versão instalada
};

struct VersionChoice {
    VersionMatch match = VersionMatch::NoVersions;
    const InstalledVersion* version = nullptr;  // a exata ou a sugerida
    bool downgrade = false;  // a sugerida é mais antiga que a do projeto (pode não abrir)
};

// Missing sugere, nesta ordem: a mais nova com o mesmo maior.menor; a mais antiga entre as mais novas que a do
// projeto (atualizar o mínimo); a padrão (que então é mais antiga: downgrade).
VersionChoice ChooseVersion(std::string_view projectVersion, const std::vector<InstalledVersion>& installed,
                            std::string_view defaultVersion);

// --- Configuração do hub (%LOCALAPPDATA%\Motor\hub.json) ----------------------------------------------------

struct HubProject {
    fs::path root;
    int64_t lastOpened = 0;  // segundos desde 1970 (UTC); 0 = nunca aberto pelo hub
};

inline constexpr const char* kDefaultServerUrl = "http://127.0.0.1:47900";  // MotorServidor nesta máquina

struct HubConfig {
    static constexpr const char* kFormat = "motor-hub";
    static constexpr int kVersion = 2;  // 2: servidores de versões (Fase 15)

    std::vector<HubProject> projects;  // mais recentes primeiro
    std::string defaultVersion;        // vazio = a mais nova instalada
    fs::path newProjectDir;            // última pasta-pai usada em Novo projeto (vazio = Documentos\Motor)
    // Servidores de versões (MotorServidor), na ordem de busca. Formato 1 (sem a lista) = o servidor local.
    std::vector<std::string> servers{kDefaultServerUrl};

    // Leitura tolerante: arquivo ausente = configuração vazia; campo ausente = padrão. `warning` recebe o motivo
    // se o arquivo existir mas não puder ser lido (a configuração volta vazia).
    static HubConfig Load(const fs::path& file, std::string* warning = nullptr);
    bool Save(const fs::path& file, std::string* error = nullptr) const;  // grava .tmp e renomeia

    HubProject* FindProject(const fs::path& root);
    bool AddProject(const fs::path& root);  // false se já estava na lista
    bool RemoveProject(const fs::path& root);
    void MarkOpened(const fs::path& root, int64_t now);  // adiciona se faltar e passa para o topo
    bool AddServer(std::string_view url);     // false se já estava (ou vazio); "x:47900/" vira "http://x:47900"
    bool RemoveServer(std::string_view url);
};

// "nunca", "agora há pouco", "há 5 min", "há 3 h", "ontem", "há 4 dias", ou a data (dd/mm/aaaa) depois de 30 dias.
std::string FormatLastOpened(int64_t lastOpened, int64_t now);
int64_t NowSeconds();

// --- Linha de comando do editor ----------------------------------------------------------------------------

std::vector<std::string> OpenProjectArgs(const fs::path& projectRoot);
std::vector<std::string> CreateProjectArgs(const fs::path& parent, std::string_view name);

// Junta os argumentos numa linha de comando do Windows (aspas e barras como o CommandLineToArgvW espera).
std::wstring BuildCommandLine(const fs::path& exe, const std::vector<std::string>& args);

// --- Hub residente (bandeja e iniciar com o Windows) ---------------------------------------------------------

// Um hub aberto por pasta de dados: a chave (16 dígitos hexadecimais) nomeia a trava e a janela escondida que
// recebe os pedidos. A mesma pasta escrita de outro jeito ("C:\Dados\" e "c:/dados") dá a mesma chave; um hub de
// teste com --data convive com o do usuário.
std::string InstanceKey(const fs::path& dataDir);
// As duas linhas do Run são a mesma (sem diferenciar maiúsculas nem / e \ no Windows)?
bool SameStartupCommand(std::string_view a, std::string_view b);

// --- MCP do Hub -----------------------------------------------------------------------------------------------

inline constexpr int kHubMcpPort = 47600;
inline constexpr const char* kHubMcpPath = "/mcphub";
inline constexpr const char* kHubMcpTokenFile = "mcp-token";  // na pasta de dados

// Token do MCP do Hub: lê o arquivo ou, sem ele (ou vazio), cria um novo (64 dígitos hexadecimais aleatórios).
// Vazio se não deu para gravar (`error` diz por quê).
std::string LoadOrCreateMcpToken(const fs::path& file, std::string* error = nullptr);

} // namespace hub
