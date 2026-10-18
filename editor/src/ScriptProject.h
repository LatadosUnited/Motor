#pragma once

// Scripts de um projeto de jogo: a pasta Scripts/ com os .cpp/.h do usuário é compilada pelo editor numa
// DLL (GameScripts.dll), com um CMakeLists gerado que usa o SDK do Motor (headers + libMotor.dll) e o mesmo
// toolchain com que o editor foi compilado.
//
//   MeuJogo/
//     Scripts/              do usuário (versionar)
//     .motor/scripts/       gerado pelo editor (pode apagar): CMakeLists, build/, DLLs carregadas

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editor::ScriptProject {

inline constexpr const char* kScriptsFolder = "Scripts";
inline constexpr const char* kTargetName = "GameScripts";

struct Paths {
    std::filesystem::path scriptsDir;  // MeuJogo/Scripts
    std::filesystem::path workDir;     // MeuJogo/.motor/scripts
    std::filesystem::path cmakeDir;    // CMakeLists.txt e ponto de entrada gerados
    std::filesystem::path binaryDir;   // árvore de build do CMake
    std::filesystem::path dll;         // DLL compilada (o editor carrega uma cópia, ver ScriptHost)
    std::filesystem::path loadDir;     // cópias carregadas
};
Paths PathsFor(const std::filesystem::path& projectRoot);

// Toolchain e SDK do Motor (bin/sdk/MotorSDK.json, gerado pelo build do Motor).
struct Toolchain {
    std::filesystem::path cmake;
    std::string generator;
    std::filesystem::path makeProgram;
    std::filesystem::path cxxCompiler;
    std::string buildType;
    std::filesystem::path sdkCMake;  // MotorSDK.cmake: alvo importado Motor::Motor
};
std::optional<Toolchain> LoadToolchain(const std::filesystem::path& sdkJson, std::string* error = nullptr);

// Pastas ao lado do executável: sdk/MotorSDK.json e templates/Scripts.
std::filesystem::path ExecutableDir();
std::filesystem::path DefaultSdkJson();
std::filesystem::path DefaultTemplatesDir();

// --- Arquivos gerados ---
std::string GenerateCMakeLists(const Paths& paths, const Toolchain& toolchain);
std::string GenerateEntryPoint();
// Grava o CMakeLists e o ponto de entrada (só se mudaram, para o CMake não reconfigurar à toa).
bool WriteBuildFiles(const Paths& paths, const Toolchain& toolchain, std::string* error = nullptr);

// Linhas de comando (argumentos já separados) para configurar e compilar.
std::vector<std::string> ConfigureCommand(const Paths& paths, const Toolchain& toolchain);
std::vector<std::string> BuildCommand(const Paths& paths, const Toolchain& toolchain);
bool NeedsConfigure(const Paths& paths);  // sem cache do CMake, ou cache sem o build gerado (configuração interrompida)
void ResetConfigure(const Paths& paths);  // apaga o cache antes de configurar de novo
// Aviso quando a pasta do projeto é funda demais: os arquivos que o CMake e o GCC criam dentro de
// .motor/scripts/build passariam do limite de 260 caracteres do Windows (o GCC falha sem explicar). "" se ok.
std::string LongPathWarning(const Paths& paths);
// Pastas a pôr no PATH do processo de build (compilador e make, com as DLLs de que dependem).
std::vector<std::filesystem::path> ToolchainPathDirs(const Toolchain& toolchain);

// --- Pasta de scripts ---
bool IsScriptFile(const std::filesystem::path& path);    // .cpp .cc .cxx .h .hpp .inl
bool IsScriptSource(const std::filesystem::path& path);  // só os compilados (.cpp .cc .cxx)
std::vector<std::filesystem::path> ListScriptFiles(const std::filesystem::path& scriptsDir);  // ordenados
// "Assinatura" da pasta (arquivos, tamanhos e datas). Mudou = algum arquivo foi criado, salvo ou apagado.
std::string Signature(const std::filesystem::path& scriptsDir);

// Aviso do sistema quando algo muda na pasta (e subpastas): reage na hora ao salvar, sem esperar a próxima
// verificação periódica. No Windows, FindFirstChangeNotification; nas outras plataformas fica inativo.
class FolderWatch {
public:
    FolderWatch() = default;
    ~FolderWatch() { Stop(); }
    FolderWatch(const FolderWatch&) = delete;
    FolderWatch& operator=(const FolderWatch&) = delete;

    bool Start(const std::filesystem::path& dir);  // false: pasta não existe (tente de novo depois)
    void Stop();
    bool IsActive() const { return m_handle != nullptr; }
    bool Changed();  // algo mudou desde a última chamada (não bloqueia)

private:
    void* m_handle = nullptr;
};

// Nome de script novo: identificador C++ (letras ASCII, dígitos e _, sem começar com dígito). Vazio = válido.
std::string ValidateScriptName(const std::string& name);

// Modelos de "Novo script" (o nome escolhido vira o nome da struct).
struct NewScriptTemplate {
    const char* id;           // "comportamento"
    const char* label;        // "Comportamento"
    const char* description;  // uma linha, para a lista do editor
};
inline constexpr const char* kDefaultScriptTemplate = "comportamento";
const std::vector<NewScriptTemplate>& NewScriptTemplates();
// Código de um script novo a partir do modelo (id desconhecido: o padrão).
std::string NewScriptSource(const std::string& name, std::string_view templateId = kDefaultScriptTemplate);
// Cria Scripts/<name>.cpp com o modelo. Falha se já existir.
bool CreateScript(const std::filesystem::path& scriptsDir, const std::string& name,
                  std::filesystem::path* created = nullptr, std::string* error = nullptr,
                  std::string_view templateId = kDefaultScriptTemplate);
// Modelos disponíveis (nomes dos .cpp em templatesDir, sem extensão).
std::vector<std::string> ListTemplates(const std::filesystem::path& templatesDir);
// Copia templatesDir/<name>.cpp para a pasta de scripts. Falha se já existir.
bool CopyTemplate(const std::filesystem::path& templatesDir, const std::string& name,
                  const std::filesystem::path& scriptsDir, std::string* error = nullptr);

// --- Saída do compilador ---
struct Diagnostic {
    enum class Kind { Error, Warning } kind = Kind::Error;
    std::string file;  // vazio se a mensagem não aponta um arquivo (linker, CMake)
    int line = 0;
    int column = 0;
    std::string message;
};
std::vector<Diagnostic> ParseDiagnostics(const std::string& log);

} // namespace editor::ScriptProject
