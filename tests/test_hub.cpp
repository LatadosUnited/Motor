// Hub do Motor (Fase 14), núcleo sem janela (hub/src/HubCore): comparação de versões, versões instaladas
// (motor-version.json) e a cópia para Versões\, leitura de projetos (.mproject 1 e 2), escolha da versão para
// abrir, hub.json, busca e a linha de comando do editor. Não liga na libMotor.dll (como o próprio hub).
#include "TestUtil.h"

#include <HubCore.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;
using namespace hub;

namespace {
void WriteText(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

// Pasta "montada" falsa: motor-version.json e um MotorEditor.exe vazio.
fs::path MakeInstall(const fs::path& dir, const std::string& version, int api = 10) {
    WriteText(dir / "motor-version.json",
              R"({"format": "motor-version", "formatVersion": 1, "version": ")" + version +
                  R"(", "scriptApiVersion": )" + std::to_string(api) + R"(, "buildType": "Debug"})");
    WriteText(dir / "MotorEditor.exe", "falso");
    WriteText(dir / "engine" / "shaders" / "a.glsl", "// shader");
    return dir;
}

InstalledVersion Fake(const std::string& version) {
    InstalledVersion v;
    v.version = version;
    return v;
}

#ifdef _WIN32
// Lê a linha de comando de volta como o editor lê (CommandLineToArgvW), já em UTF-8.
std::vector<std::string> ParseBack(const std::wstring& line) {
    std::vector<std::string> out;
    int count = 0;
    LPWSTR* argv = CommandLineToArgvW(line.c_str(), &count);
    for (int i = 0; argv && i < count; ++i) out.push_back(ToUtf8(fs::path(argv[i])));
    LocalFree(argv);
    return out;
}
#endif
} // namespace

int main(int argc, char** argv) {
    const fs::path base = TestDir(argc, argv, "test_hub_tmp") / PathFromUtf8("Hub Ação");
    fs::create_directories(base);

    SECTION("ParseVersion / CompareVersions");
    CHECK(ParseVersion("0.2.0").valid && ParseVersion("0.2.0").minor == 2);
    CHECK(ParseVersion("1.2").valid && ParseVersion("1.2").patch == 0);
    CHECK(ParseVersion("1.2.3-beta").valid && ParseVersion("1.2.3-beta").patch == 3);
    CHECK(!ParseVersion("").valid);
    CHECK(!ParseVersion("1").valid);
    CHECK(!ParseVersion("1.2.").valid);
    CHECK(!ParseVersion("a.b.c").valid);
    CHECK(!ParseVersion("1.2.3.4").valid);
    CHECK(CompareVersions("0.2.0", "0.10.0") < 0);  // numérico, não alfabético
    CHECK(CompareVersions("1.0.0", "0.99.99") > 0);
    CHECK(CompareVersions("0.2", "0.2.0") == 0);
    CHECK(CompareVersions("0.2.1", "0.2.0") > 0);
    CHECK(CompareVersions("lixo", "0.0.1") < 0);

    SECTION("Versões instaladas");
    const fs::path built = MakeInstall(base / PathFromUtf8("Montada Ação"), "0.2.0");
    std::string error;
    auto read = ReadInstalledVersion(built, &error);
    CHECK(read.has_value());
    if (read) {
        CHECK(read->version == "0.2.0");
        CHECK(read->scriptApiVersion == 10);
        CHECK(read->editor == built / "MotorEditor.exe");
    }
    CHECK(!ReadInstalledVersion(base / "nada", &error).has_value());
    WriteText(base / "sem-editor" / "motor-version.json", R"({"format": "motor-version", "version": "0.3.0"})");
    CHECK(!ReadInstalledVersion(base / "sem-editor", &error).has_value());
    CHECK(error.find("editor") != std::string::npos);

    SECTION("InstallVersion copia para Versões\\<versão>");
    const fs::path versions = base / PathFromUtf8("Versões");
    auto installed = InstallVersion(built, versions, &error);
    CHECK(installed.has_value());
    if (!installed) std::printf("  erro: %s\n", error.c_str());
    CHECK(fs::is_regular_file(versions / "0.2.0" / "motor-version.json"));
    CHECK(fs::is_regular_file(versions / "0.2.0" / "engine" / "shaders" / "a.glsl"));
    CHECK(!fs::exists(versions / "0.2.0.tmp"));
    CHECK(!InstallVersion(built, versions, &error).has_value());  // já instalada
    CHECK(error.find("já está instalada") != std::string::npos);
    CHECK(!InstallVersion(versions / "0.2.0", versions, &error).has_value());  // a própria pasta
    CHECK(!InstallVersion(base / "nada", versions, &error).has_value());

    InstallVersion(MakeInstall(base / "m010", "0.10.0", 12), versions, &error);
    InstallVersion(MakeInstall(base / "m011", "0.1.1", 9), versions, &error);
    fs::create_directories(versions / "0.9.0.tmp");  // cópia interrompida: ignorada
    WriteText(versions / "lixo" / "leia.txt", "não é versão");
    const std::vector<InstalledVersion> scanned = ScanVersions(versions);
    CHECK(scanned.size() == 3);
    if (scanned.size() == 3) {
        CHECK(scanned[0].version == "0.10.0");  // mais nova primeiro
        CHECK(scanned[1].version == "0.2.0");
        CHECK(scanned[2].version == "0.1.1");
    }
    CHECK(DefaultVersion(scanned, "0.2.0") && DefaultVersion(scanned, "0.2.0")->version == "0.2.0");
    CHECK(DefaultVersion(scanned, "") && DefaultVersion(scanned, "")->version == "0.10.0");
    CHECK(DefaultVersion(scanned, "9.9.9") && DefaultVersion(scanned, "9.9.9")->version == "0.10.0");
    CHECK(DefaultVersion({}, "") == nullptr);

    SECTION("ChooseVersion");
    const std::vector<InstalledVersion> some = {Fake("0.3.0"), Fake("0.2.5"), Fake("0.2.1"), Fake("0.1.0")};
    VersionChoice c = ChooseVersion("0.2.1", some, "");
    CHECK(c.match == VersionMatch::Exact && c.version->version == "0.2.1" && !c.downgrade);
    c = ChooseVersion("", some, "0.2.5");
    CHECK(c.match == VersionMatch::Unknown && c.version->version == "0.2.5");
    c = ChooseVersion("", some, "");
    CHECK(c.match == VersionMatch::Unknown && c.version->version == "0.3.0");
    c = ChooseVersion("0.2.0", some, "0.1.0");  // ausente: a mais nova do mesmo 0.2
    CHECK(c.match == VersionMatch::Missing && c.version->version == "0.2.5" && !c.downgrade);
    c = ChooseVersion("0.2.9", some, "");  // 0.2.9 > 0.2.5: mesma série, mas mais antiga
    CHECK(c.match == VersionMatch::Missing && c.version->version == "0.2.5" && c.downgrade);
    c = ChooseVersion("0.1.5", some, "");  // sem 0.1.x maior: a mais antiga entre as mais novas
    CHECK(c.match == VersionMatch::Missing && c.version->version == "0.1.0");
    c = ChooseVersion("0.0.9", some, "");
    CHECK(c.match == VersionMatch::Missing && c.version->version == "0.1.0" && !c.downgrade);
    c = ChooseVersion("1.0.0", some, "0.2.1");  // projeto mais novo que todas: a padrão, com aviso
    CHECK(c.match == VersionMatch::Missing && c.version->version == "0.2.1" && c.downgrade);
    c = ChooseVersion("0.2.0", {}, "");
    CHECK(c.match == VersionMatch::NoVersions && c.version == nullptr);

    SECTION("ReadProject (formatos 1 e 2)");
    const fs::path game = base / PathFromUtf8("Jogo Ação");
    WriteText(game / PathFromUtf8("Jogo Ação.mproject"),
              R"({"format": "motor-project", "version": 2, "name": "Jogo Ação", "engineVersion": "0.2.0"})");
    ProjectInfo info = ReadProject(game);
    CHECK(info.found && info.name == "Jogo Ação" && info.engineVersion == "0.2.0");
    CHECK(info.file.filename() == PathFromUtf8("Jogo Ação.mproject"));
    info = ReadProject(game / PathFromUtf8("Jogo Ação.mproject"));  // pelo arquivo
    CHECK(info.found && SamePath(info.root, game));
    const fs::path old = base / "Antigo";
    WriteText(old / "Antigo.mproject", R"({"format": "motor-project", "version": 1, "name": "Antigo"})");
    info = ReadProject(old);
    CHECK(info.found && info.engineVersion.empty());
    info = ReadProject(base / "Sumiu");
    CHECK(!info.found && !info.error.empty() && info.name == "Sumiu");
    WriteText(base / "Outro" / "x.mproject", R"({"format": "outra-coisa"})");
    CHECK(!ReadProject(base / "Outro").found);
    WriteText(base / "Quebrado" / "q.mproject", "{ não é json");
    CHECK(!ReadProject(base / "Quebrado").found);

    SECTION("Novo projeto: nome e pasta");
    CHECK(ValidateProjectName("Jogo Ação 2").empty());
    CHECK(!ValidateProjectName("").empty());
    CHECK(!ValidateProjectName("a:b").empty());
    CHECK(!ValidateProjectName("nome.").empty());
    CHECK(CheckNewProject(base, "Novo Ação").empty());
    CHECK(!CheckNewProject(base, "Jogo Ação").empty());  // já existe e não está vazia
    fs::create_directories(base / "Vazia");
    CHECK(CheckNewProject(base, "Vazia").empty());
    CHECK(!CheckNewProject(base / "não existe", "X").empty());

    SECTION("hub.json: salvar, ler, tolerância");
    const fs::path configFile = base / "dados" / "hub.json";
    HubConfig config;
    CHECK(config.AddProject(game));
    CHECK(!config.AddProject(game / "."));  // o mesmo projeto
    CHECK(config.AddProject(old));
    CHECK(config.projects.front().root == old);  // adicionado por último fica em cima
    config.MarkOpened(game, 1'700'000'000);
    CHECK(config.projects.size() == 2 && SamePath(config.projects.front().root, game));
    CHECK(config.projects.front().lastOpened == 1'700'000'000);
    config.MarkOpened(base / "Novo", 1'700'000'100);  // ainda não estava na lista
    CHECK(config.projects.size() == 3);
    config.defaultVersion = "0.2.0";
    config.newProjectDir = base;
    CHECK(config.Save(configFile, &error));
    CHECK(!fs::exists(fs::path(configFile) += ".tmp"));
#ifdef _WIN32
    {
        // hub.json preso por um instante (antivírus lendo): a gravação espera e grava (motor::WriteFileAtomic).
        std::atomic<bool> opened{false};
        std::thread holder([&] {
            std::ifstream in(configFile, std::ios::binary);
            opened = in.is_open();
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        });
        for (int i = 0; i < 200 && !opened; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool saved = config.Save(configFile, &error);
        holder.join();
        if (!saved) std::printf("  erro: %s\n", error.c_str());
        CHECK(opened.load() && saved);
    }
#endif

    HubConfig loaded = HubConfig::Load(configFile, &error);
    CHECK(loaded.projects.size() == 3);
    CHECK(loaded.defaultVersion == "0.2.0");
    CHECK(SamePath(loaded.newProjectDir, base));
    CHECK(loaded.FindProject(game) && loaded.FindProject(game)->lastOpened == 1'700'000'000);
    CHECK(loaded.RemoveProject(old));
    CHECK(!loaded.RemoveProject(old));
    CHECK(loaded.projects.size() == 2);

    std::string warning;
    CHECK(HubConfig::Load(base / "não existe.json", &warning).projects.empty() && warning.empty());
    WriteText(base / "velho.json", R"({"format": "motor-hub", "version": 1, "projects": [{"path": "C:/X"}, {}, 3]})");
    loaded = HubConfig::Load(base / "velho.json", &warning);
    CHECK(loaded.projects.size() == 1 && loaded.projects[0].lastOpened == 0 && loaded.defaultVersion.empty());
    WriteText(base / "futuro.json", R"({"format": "motor-hub", "version": 7, "defaultVersion": "1.0.0", "novo": true})");
    CHECK(HubConfig::Load(base / "futuro.json", &warning).defaultVersion == "1.0.0");
    WriteText(base / "ruim.json", "[1, 2");
    warning.clear();
    CHECK(HubConfig::Load(base / "ruim.json", &warning).projects.empty() && !warning.empty());

    SECTION("Busca");
    CHECK(MatchesSearch("Jogo Ação", ""));
    CHECK(MatchesSearch("Jogo Ação", "jogo"));
    CHECK(MatchesSearch("Jogo Ação", "AÇÃO"));
    CHECK(MatchesSearch("JOGO AÇÃO", "ação"));
    CHECK(!MatchesSearch("Jogo Ação", "acao"));  // sem tirar acentos
    CHECK(!MatchesSearch("Jogo", "Corrida"));

    SECTION("Última abertura");
    const int64_t now = 1'800'000'000;
    CHECK(FormatLastOpened(0, now) == "nunca");
    CHECK(FormatLastOpened(now - 30, now) == "agora há pouco");
    CHECK(FormatLastOpened(now - 5 * 60, now) == "há 5 min");
    CHECK(FormatLastOpened(now - 3 * 3600, now) == "há 3 h");
    CHECK(FormatLastOpened(now - 30 * 3600, now) == "ontem");
    CHECK(FormatLastOpened(now - 4 * 86400, now) == "há 4 dias");
    CHECK(FormatLastOpened(now - 90 * 86400, now).size() == 10);  // dd/mm/aaaa
    CHECK(FormatLastOpened(now + 100, now) == "agora há pouco");    // relógio adiantado

    SECTION("Linha de comando do editor");
    const std::vector<std::string> open = OpenProjectArgs(game);
    CHECK(open.size() == 2 && open[0] == "--project" && open[1] == ToUtf8(game));
    const std::vector<std::string> create = CreateProjectArgs(base, "Jogo Ação");
    CHECK(create.size() == 3 && create[0] == "--create" && create[2] == "Jogo Ação");
#ifdef _WIN32
    const fs::path exe = versions / "0.2.0" / "MotorEditor.exe";
    const std::vector<std::string> tricky = {"--create", "C:\\Pasta Ação\\", "Jogo \"Ação\"", "", "simples",
                                             "C:\\a\\b c\\\\", "--no-settings"};
    const std::vector<std::string> back = ParseBack(BuildCommandLine(exe, tricky));
    CHECK(back.size() == tricky.size() + 1);
    if (back.size() == tricky.size() + 1) {
        CHECK(SamePath(PathFromUtf8(back[0]), exe));
        bool same = true;
        for (size_t i = 0; i < tricky.size(); ++i) same = same && back[i + 1] == tricky[i];
        CHECK(same);
    }
#endif

    SECTION("Hub residente: chave da instância e linha do iniciar com o Windows");
    {
        const fs::path data = base / PathFromUtf8("Dados Ação");
        const std::string key = InstanceKey(data);
        CHECK(key.size() == 16 && key.find_first_not_of("0123456789abcdef") == std::string::npos);
        CHECK(InstanceKey(data) == key);                           // estável
        CHECK(InstanceKey(base / "Outra") != key);                 // outra pasta, outro hub
        CHECK(InstanceKey(data / "sub" / "..") == key);            // normalizada
        CHECK(InstanceKey(PathFromUtf8(ToUtf8(data) + "\\")) == key);  // barra no fim não conta
#ifdef _WIN32
        std::string upper = ToUtf8(data);
        for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        CHECK(InstanceKey(PathFromUtf8(upper)) == key);  // o Windows não diferencia maiúsculas
        CHECK(SameStartupCommand(R"("C:\Hub\MotorHub.exe" --bandeja)", R"("c:/hub/motorhub.exe" --bandeja)"));
#endif
        CHECK(SameStartupCommand(R"("C:\Hub\MotorHub.exe" --bandeja)", R"("C:\Hub\MotorHub.exe" --bandeja)"));
        CHECK(!SameStartupCommand(R"("C:\Hub\MotorHub.exe" --bandeja)",
                                  R"("C:\Hub\MotorHub.exe" --bandeja --data "C:\Dados")"));
    }

    SECTION("Token do MCP do Hub: cria uma vez e depois só lê");
    {
        const fs::path file = base / PathFromUtf8("Dados Ação") / "mcp-token";
        std::error_code ec;
        fs::remove(file, ec);
        const std::string token = LoadOrCreateMcpToken(file);
        CHECK(token.size() == 64 && token.find_first_not_of("0123456789abcdef") == std::string::npos);
        CHECK(fs::exists(file));
        CHECK(LoadOrCreateMcpToken(file) == token);  // o mesmo depois de reabrir
        {
            std::ofstream out(file, std::ios::binary);
            out << "  meu-token  \r\n";  // editado à mão: espaços e quebra de linha não contam
        }
        CHECK(LoadOrCreateMcpToken(file) == "meu-token");
        {
            std::ofstream out(file, std::ios::binary);  // vazio: cria outro
        }
        const std::string other = LoadOrCreateMcpToken(file);
        CHECK(other.size() == 64 && other != token);
    }

    SECTION("Ferramentas do MCP do editor no pacote da versão (mcp-ferramentas.json)");
    {
        const fs::path dir = MakeInstall(base / PathFromUtf8("Ferramentas Ação"), "0.4.0");
        std::string error;
        CHECK(!ReadEditorTools(dir, &error));  // versão antiga: sem o arquivo
        CHECK(error.find(kMcpToolsFile) != std::string::npos);

        WriteText(dir / kMcpToolsFile, R"({"format": "outro", "tools": []})");
        CHECK(!ReadEditorTools(dir));
        WriteText(dir / kMcpToolsFile, "{quebrado");
        CHECK(!ReadEditorTools(dir));

        WriteText(dir / kMcpToolsFile,
                  R"({"format": "motor-mcp-tools", "version": 1, "engineVersion": "0.4.0",
                      "initialize": {"serverInfo": {"name": "motor-editor"}, "instructions": "Cena e Play."},
                      "tools": [{"name": "scene_tree", "description": "Árvore da cena", "inputSchema": {}},
                                {"name": "play", "description": "Joga", "inputSchema": {}}]})");
        const auto tools = ReadEditorTools(dir);
        CHECK(tools.has_value());
        CHECK(tools && tools->engineVersion == "0.4.0");
        CHECK(tools && tools->tools.size() == 2 && tools->tools[0]["name"] == "scene_tree");
        CHECK(tools && tools->tools[0]["description"] == "Árvore da cena");
        CHECK(tools && tools->initialize["instructions"] == "Cena e Play.");

        // Formato mais novo: o que este Hub conhece continua sendo lido.
        WriteText(dir / kMcpToolsFile,
                  R"({"format": "motor-mcp-tools", "version": 2, "novidade": true, "tools": [{"name": "x"}]})");
        const auto newer = ReadEditorTools(dir);
        CHECK(newer && newer->tools.size() == 1 && newer->initialize.is_object());
    }

    return TestResult();
}
