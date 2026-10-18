#include "ScriptProject.h"

#include <motor/core/Paths.h>
#include <motor/core/Version.h>
#include <motor/scene/ScriptModule.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <regex>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace editor::ScriptProject {

namespace fs = std::filesystem;
using motor::ToGenericUtf8;
using motor::ToUtf8;

namespace {
void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Grava só se o conteúdo mudou (preserva a data do arquivo, que o make e o CMake usam).
bool WriteIfChanged(const fs::path& path, const std::string& text, std::string* error) {
    std::error_code ec;
    if (fs::is_regular_file(path, ec) && ReadFile(path) == text) return true;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out || !(out << text) || !out.flush()) {
        SetError(error, std::format("Não foi possível escrever '{}'.", ToUtf8(path)));
        return false;
    }
    return true;
}

// Limite de caminho do Windows (MAX_PATH) e o tamanho máximo que o CMake deixa para o caminho de um objeto.
// A folga cobre o ".d" de dependências que o GCC grava ao lado do .obj (o CMake conta bytes UTF-8, que já
// são mais que os caracteres do Windows quando há acentos).
constexpr size_t kMaxPath = 260;
constexpr size_t kObjectPathMax = kMaxPath - 20;

// Caminho dentro de uma string do CMake: barras normais e sem caracteres especiais soltos.
std::string CMakePath(const fs::path& path) {
    std::string out;
    for (char c : ToGenericUtf8(path)) {
        if (c == '"' || c == '\\' || c == '$' || c == ';') out += '\\';
        out += c;
    }
    return out;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

Paths PathsFor(const fs::path& projectRoot) {
    Paths p;
    p.scriptsDir = projectRoot / kScriptsFolder;
    p.workDir = projectRoot / ".motor" / "scripts";
    p.cmakeDir = p.workDir / "cmake";
    p.binaryDir = p.workDir / "build";
    p.dll = p.binaryDir / (std::string(kTargetName) + ".dll");
    p.loadDir = p.workDir / "loaded";
    return p;
}

std::optional<Toolchain> LoadToolchain(const fs::path& sdkJson, std::string* error) {
    std::error_code ec;
    if (!fs::is_regular_file(sdkJson, ec)) {
        SetError(error, std::format("SDK do Motor não encontrado: '{}'. Compile o Motor (cmake --build) antes.",
                                    ToUtf8(sdkJson)));
        return std::nullopt;
    }
    try {
        const auto j = nlohmann::json::parse(ReadFile(sdkJson));
        if (j.value("format", std::string()) != "motor-sdk") {
            SetError(error, std::format("'{}' não é um SDK do Motor.", ToUtf8(sdkJson)));
            return std::nullopt;
        }
        Toolchain t;
        t.cmake = motor::PathFromUtf8(j.value("cmake", std::string()));
        t.generator = j.value("generator", std::string());
        t.makeProgram = motor::PathFromUtf8(j.value("makeProgram", std::string()));
        t.cxxCompiler = motor::PathFromUtf8(j.value("cxxCompiler", std::string()));
        t.buildType = j.value("buildType", std::string("Debug"));
        t.sdkCMake = motor::PathFromUtf8(j.value("sdkCMake", std::string()));
        // Pasta instalada (alvo motor_package): o MotorSDK.cmake fica ao lado do .json, com caminho relativo.
        if (!t.sdkCMake.empty() && t.sdkCMake.is_relative()) t.sdkCMake = sdkJson.parent_path() / t.sdkCMake;
        if (t.cmake.empty() || t.generator.empty() || t.cxxCompiler.empty() || t.sdkCMake.empty()) {
            SetError(error, std::format("SDK do Motor incompleto: '{}'.", ToUtf8(sdkJson)));
            return std::nullopt;
        }
        return t;
    } catch (const nlohmann::json::exception& e) {
        SetError(error, std::format("SDK do Motor inválido ('{}'): {}", ToUtf8(sdkJson), e.what()));
        return std::nullopt;
    }
}

fs::path ExecutableDir() {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n == 0) return {};
        if (n < buffer.size()) {
            buffer.resize(n);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return fs::path(buffer).parent_path();
#else
    std::error_code ec;
    return fs::canonical("/proc/self/exe", ec).parent_path();
#endif
}

fs::path DefaultSdkJson() {
    return ExecutableDir() / "sdk" / "MotorSDK.json";
}

fs::path DefaultTemplatesDir() {
    return ExecutableDir() / "templates" / kScriptsFolder;
}

// ---------------------------------------------------------------------------
// Arquivos gerados
// ---------------------------------------------------------------------------

std::string GenerateCMakeLists(const Paths& paths, const Toolchain& toolchain) {
    std::string s;
    s += "# Gerado pelo Motor Editor. Não edite: as mudanças seriam perdidas.\n";
    s += std::format("# Versão do Motor: {} (API de scripts {})\n", motor::kVersion, motor::kScriptApiVersion);
    s += "# Scripts do projeto: " + ToGenericUtf8(paths.scriptsDir) + "\n";
    s += "cmake_minimum_required(VERSION 3.24)\n";
    s += "# Objetos com nomes curtos (hash) em vez do caminho inteiro do script: o as.exe do binutils lê o caminho\n";
    s += "# de saída fora do UTF-8 (acentos viram lixo e ele não acha a pasta) e o caminho não passa de 260 caracteres.\n";
    s += "# Só funciona em cache e antes do project().\n";
    s += "if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.2)\n";
    s += "    set(CMAKE_INTERMEDIATE_DIR_STRATEGY SHORT CACHE STRING \"\")\n";
    s += "endif()\n";
    s += std::format("project({} LANGUAGES CXX)\n\n", kTargetName);
    s += "set(CMAKE_CXX_STANDARD 20)\n";
    s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    s += "set(CMAKE_CXX_EXTENSIONS OFF)\n";
    s += "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n";
    s += "# Os .obj dos scripts ficam em pastas com o caminho do projeto (acentos). O ar.exe lê o arquivo de\n";
    s += "# resposta da lista de objetos fora do UTF-8 e não os acha: passa a lista na linha de comando.\n";
    s += "set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_OBJECTS OFF)\n";
    s += "# O mesmo para as bibliotecas (o ld lê o arquivo de resposta delas fora do UTF-8): a libMotor.dll.a de uma\n";
    s += "# versão instalada pelo Hub fica em %LOCALAPPDATA%/Motor/Versões/<versão>/sdk/lib.\n";
    s += "set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_LIBRARIES OFF)\n";
    s += "# CMake antes do 4.2: os objetos levam o caminho inteiro do script (C_/.../Scripts/X.cpp.obj) e passam fácil\n";
    s += "# do limite de 260 caracteres do Windows. Acima deste tamanho o CMake troca as pastas por um hash.\n";
    s += std::format("set(CMAKE_OBJECT_PATH_MAX {})\n\n", kObjectPathMax);
    s += "# SDK do Motor (headers e libMotor.dll do mesmo build do editor).\n";
    s += std::format("include(\"{}\")\n\n", CMakePath(toolchain.sdkCMake));
    s += "# Fontes da pasta Scripts (lista explícita para não rodar VerifyGlobs a cada compilação; atualizada\n";
    s += "# pelo editor só quando entram ou saem arquivos).\n";
    s += "set(MOTOR_SCRIPT_SOURCES\n";
    for (const fs::path& file : ListScriptFiles(paths.scriptsDir)) {
        if (IsScriptSource(file)) s += std::format("    \"{}\"\n", CMakePath(file));
    }
    s += ")\n";
    s += std::format("add_library({} SHARED \"${{CMAKE_CURRENT_LIST_DIR}}/MotorScriptEntry.cpp\" ${{MOTOR_SCRIPT_SOURCES}})\n",
                     kTargetName);
    s += std::format("target_include_directories({} PRIVATE \"{}\")\n", kTargetName, CMakePath(paths.scriptsDir));
    s += std::format("target_link_libraries({} PRIVATE Motor::Motor)\n", kTargetName);
    s += "if(CMAKE_CXX_COMPILER_ID MATCHES \"GNU|Clang\")\n";
    s += std::format("    target_compile_options({} PRIVATE -Wall -Wextra)\n", kTargetName);
    s += "    if(DEFINED MOTOR_SDK_PCH AND EXISTS \"${MOTOR_SDK_PCH}.gch\")\n";
    s += std::format("        target_compile_options({} PRIVATE -Winvalid-pch -include \"${{MOTOR_SDK_PCH}}\")\n", kTargetName);
    s += "    endif()\n";
    s += "endif()\n";
    s += std::format("set_target_properties({0} PROPERTIES PREFIX \"\" OUTPUT_NAME {0}\n", kTargetName);
    s += std::format("    RUNTIME_OUTPUT_DIRECTORY \"{0}\" LIBRARY_OUTPUT_DIRECTORY \"{0}\")\n",
                     CMakePath(paths.dll.parent_path()));
    return s;
}

std::string GenerateEntryPoint() {
    return std::format(
        "// Gerado pelo Motor Editor: ponto de entrada da DLL de scripts. Não edite.\n"
        "#include <motor/scene/ScriptModule.h>\n\n"
        "extern \"C\" __declspec(dllexport) int {}() {{\n"
        "    return motor::kScriptApiVersion;\n"
        "}}\n\n"
        "// Chama todos os blocos MOTOR_SCRIPT dos arquivos da pasta Scripts.\n"
        "extern \"C\" __declspec(dllexport) void {}(motor::ComponentRegistry& registry) {{\n"
        "    motor::detail::RegisterModuleScripts(registry);\n"
        "}}\n",
        motor::kScriptApiVersionSymbol, motor::kRegisterScriptsSymbol);
}

bool WriteBuildFiles(const Paths& paths, const Toolchain& toolchain, std::string* error) {
    std::error_code ec;
    fs::create_directories(paths.cmakeDir, ec);
    if (ec) {
        SetError(error, std::format("Não foi possível criar '{}': {}", ToUtf8(paths.cmakeDir), ec.message()));
        return false;
    }
    const std::string stamp = std::format("{}|{}|{}|{}|{}|{}|{}|{}\n",
                                          motor::kVersion, motor::kScriptApiVersion,
                                          ToGenericUtf8(toolchain.cmake), toolchain.generator,
                                          ToGenericUtf8(toolchain.makeProgram), ToGenericUtf8(toolchain.cxxCompiler),
                                          toolchain.buildType, ToGenericUtf8(toolchain.sdkCMake));
    const fs::path stampFile = paths.cmakeDir / "toolchain.stamp";
    if (fs::is_regular_file(stampFile, ec) && ReadFile(stampFile) != stamp) {
        ResetConfigure(paths);
    }
    return WriteIfChanged(stampFile, stamp, error) &&
           WriteIfChanged(paths.cmakeDir / "CMakeLists.txt", GenerateCMakeLists(paths, toolchain), error) &&
           WriteIfChanged(paths.cmakeDir / "MotorScriptEntry.cpp", GenerateEntryPoint(), error);
}

std::vector<std::string> ConfigureCommand(const Paths& paths, const Toolchain& toolchain) {
    std::vector<std::string> args = {
        ToUtf8(toolchain.cmake), "-S", ToUtf8(paths.cmakeDir), "-B", ToUtf8(paths.binaryDir),
        "-G", toolchain.generator,
        "-DCMAKE_BUILD_TYPE=" + toolchain.buildType,
        "-DCMAKE_CXX_COMPILER=" + ToGenericUtf8(toolchain.cxxCompiler),
    };
    if (!toolchain.makeProgram.empty()) args.push_back("-DCMAKE_MAKE_PROGRAM=" + ToGenericUtf8(toolchain.makeProgram));
    return args;
}

std::vector<std::string> BuildCommand(const Paths& paths, const Toolchain& toolchain) {
    return {ToUtf8(toolchain.cmake), "--build", ToUtf8(paths.binaryDir), "--parallel"};
}

bool NeedsConfigure(const Paths& paths) {
    std::error_code ec;
    if (!fs::is_regular_file(paths.binaryDir / "CMakeCache.txt", ec)) return true;
    // Cache sem o build gerado: configuração interrompida (editor fechado no meio) ou que falhou. Só o
    // "cmake --build" não sai disso ("make: Makefile: No such file").
    for (const char* file : {"Makefile", "build.ninja"}) {
        if (fs::is_regular_file(paths.binaryDir / file, ec)) return false;
    }
    return true;
}

std::string LongPathWarning(const Paths& paths) {
    // O mais longo é o teste do compilador do CMake: build/CMakeFiles/CMakeScratch/TryCompile-xxxxxx/CMakeFiles/
    // cmTC_xxxxx.dir/testCXXCompiler.cxx.obj (~92 a mais). Os objetos dos scripts o CMake encurta com um hash
    // (CMAKE_INTERMEDIATE_DIR_STRATEGY e CMAKE_OBJECT_PATH_MAX no CMakeLists gerado).
    if (paths.binaryDir.native().size() + 92 < kMaxPath) return {};
    const fs::path project = paths.scriptsDir.parent_path();
    return std::format("o caminho da pasta do projeto é longo demais ({} caracteres): os arquivos da compilação "
                       "passariam do limite de {} do Windows e o compilador falha. Mova o projeto para uma pasta "
                       "mais curta (ex.: C:\\Jogos\\{}).",
                       project.native().size(), kMaxPath, ToUtf8(project.filename()));
}

void ResetConfigure(const Paths& paths) {
    // Começa do zero: um cache pela metade pode ter outro gerador ou compilador e fazer a configuração falhar.
    std::error_code ec;
    fs::remove(paths.binaryDir / "CMakeCache.txt", ec);
    fs::remove_all(paths.binaryDir / "CMakeFiles", ec);
}

std::vector<fs::path> ToolchainPathDirs(const Toolchain& toolchain) {
    std::vector<fs::path> dirs;
    for (const fs::path& tool : {toolchain.cxxCompiler, toolchain.makeProgram}) {
        if (tool.empty()) continue;
        const fs::path dir = tool.parent_path();
        if (std::find(dirs.begin(), dirs.end(), dir) == dirs.end()) dirs.push_back(dir);
    }
    return dirs;
}

// ---------------------------------------------------------------------------
// Pasta de scripts
// ---------------------------------------------------------------------------

bool IsScriptSource(const fs::path& path) {
    const std::string ext = Lower(ToUtf8(path.extension()));
    return ext == ".cpp" || ext == ".cc" || ext == ".cxx";
}

bool IsScriptFile(const fs::path& path) {
    const std::string ext = Lower(ToUtf8(path.extension()));
    return IsScriptSource(path) || ext == ".h" || ext == ".hpp" || ext == ".inl";
}

std::vector<fs::path> ListScriptFiles(const fs::path& scriptsDir) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::is_directory(scriptsDir, ec)) return files;
    for (auto it = fs::recursive_directory_iterator(scriptsDir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec) && IsScriptFile(it->path())) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string Signature(const fs::path& scriptsDir) {
    std::string sig;
    for (const fs::path& file : ListScriptFiles(scriptsDir)) {
        std::error_code ec;
        const auto size = fs::file_size(file, ec);
        const auto time = fs::last_write_time(file, ec).time_since_epoch().count();
        sig += std::format("{}|{}|{}\n", ToGenericUtf8(file.lexically_relative(scriptsDir)), size, time);
    }
    return sig;
}

std::string ValidateScriptName(const std::string& name) {
    if (name.empty()) return "O nome não pode ficar vazio.";
    if (name.size() > 64) return "O nome deve ter no máximo 64 caracteres.";
    if (std::isdigit(static_cast<unsigned char>(name[0]))) return "O nome não pode começar com número.";
    for (unsigned char c : name) {
        if (!(std::isalnum(c) && c < 128) && c != '_') {
            return "Use só letras sem acento, números e _ (o nome vira o nome de uma struct em C++).";
        }
    }
    return {};
}

std::string NewScriptSource(const std::string& name) {
    return std::format(R"(// Script {0}. Os campos registrados no bloco MOTOR_SCRIPT aparecem no Inspector e são salvos na cena;
// os métodos On* só rodam no modo Play. Salve o arquivo e o editor recompila e recarrega os scripts.

#include <motor/scene/ScriptModule.h>

struct {0} : motor::Script {{
    float velocidade = 1.0f;

    void OnStart() override {{
        Log("{0} começou");
    }}

    void OnUpdate(float dt) override {{
        // Ex.: GetTransform().position.y += velocidade * dt;
        (void)dt;
    }}
}};

MOTOR_SCRIPT({0}) {{
    registry.Register<{0}>("{0}", "{0}")
        .Field("velocidade", "Velocidade", &{0}::velocidade);
}}
)",
                       name);
}

bool CreateScript(const fs::path& scriptsDir, const std::string& name, fs::path* created, std::string* error) {
    if (std::string problem = ValidateScriptName(name); !problem.empty()) {
        SetError(error, std::move(problem));
        return false;
    }
    std::error_code ec;
    fs::create_directories(scriptsDir, ec);
    const fs::path file = scriptsDir / (name + ".cpp");
    if (fs::exists(file, ec)) {
        SetError(error, std::format("Já existe '{}'.", ToUtf8(file.filename())));
        return false;
    }
    if (!WriteIfChanged(file, NewScriptSource(name), error)) return false;
    if (created) *created = file;
    return true;
}

std::vector<std::string> ListTemplates(const fs::path& templatesDir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(templatesDir, ec)) {
        if (entry.is_regular_file(ec) && IsScriptSource(entry.path())) names.push_back(ToUtf8(entry.path().stem()));
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool CopyTemplate(const fs::path& templatesDir, const std::string& name, const fs::path& scriptsDir,
                  std::string* error) {
    std::error_code ec;
    const fs::path source = templatesDir / motor::PathFromUtf8(name + ".cpp");
    const fs::path target = scriptsDir / source.filename();
    if (!fs::is_regular_file(source, ec)) {
        SetError(error, std::format("Modelo de script não encontrado: '{}'.", ToUtf8(source)));
        return false;
    }
    if (fs::exists(target, ec)) {
        SetError(error, std::format("Já existe '{}' na pasta de scripts.", ToUtf8(target.filename())));
        return false;
    }
    fs::create_directories(scriptsDir, ec);
    fs::copy_file(source, target, ec);
    if (ec) {
        SetError(error, std::format("Não foi possível copiar o modelo '{}': {}", name, ec.message()));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Saída do compilador
// ---------------------------------------------------------------------------

std::vector<Diagnostic> ParseDiagnostics(const std::string& log) {
    // GCC/Clang: "C:/MeuJogo/Scripts/Girar.cpp:12:5: error: mensagem" (o caminho pode ter ':' do drive).
    static const std::regex compiler(R"(^(.+?):(\d+):(?:(\d+):)? (fatal error|error|warning): (.*)$)");
    std::vector<Diagnostic> result;
    std::istringstream in(log);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::smatch m;
        if (std::regex_match(line, m, compiler)) {
            Diagnostic d;
            d.kind = m[4] == "warning" ? Diagnostic::Kind::Warning : Diagnostic::Kind::Error;
            d.file = m[1];
            d.line = std::stoi(m[2]);
            d.column = m[3].matched ? std::stoi(m[3]) : 0;
            d.message = m[5];
            result.push_back(std::move(d));
        } else if (line.find("undefined reference to") != std::string::npos ||
                   line.rfind("CMake Error", 0) == 0 || line.find("collect2.exe: error") != std::string::npos ||
                   line.find("ld.exe: cannot") != std::string::npos) {
            // Linker e CMake: sem arquivo/linha confiável; a linha inteira é a mensagem.
            result.push_back({Diagnostic::Kind::Error, {}, 0, 0, line});
        }
    }
    return result;
}

} // namespace editor::ScriptProject
