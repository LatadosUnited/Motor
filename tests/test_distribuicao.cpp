// Distribuição das versões (Fase 15), sem janela: SHA-256 (BCrypt), caminhos e URLs, manifesto (leitura
// tolerante), pasta de publicações do servidor, plano de instalação e de desinstalação do Hub, hub.json formato 2
// e, em loopback numa porta livre, o servidor (server/src/ServerCore) com o cliente: manifesto, página, Range,
// caminhos recusados, download de uma versão inteira, cancelar e retomar, arquivo corrompido e o Hub publicado.
#include <httplib.h>  // antes de qualquer windows.h (winsock2)

#include "TestUtil.h"

#include <Download.h>
#include <HubCore.h>
#include <InstallPlan.h>
#include <Manifest.h>
#include <ServerCore.h>
#include <Sha256.h>

#include <atomic>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace distrib;
using hub::PathFromUtf8;
using hub::ToUtf8;

namespace {
void WriteText(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string ReadText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Conteúdo "aleatório" previsível (não comprime, muda a cada byte).
std::string Noise(size_t size, unsigned seed) {
    std::string out(size, '\0');
    unsigned x = seed * 2654435761u + 1;
    for (char& c : out) {
        x = x * 1664525u + 1013904223u;
        c = static_cast<char>(x >> 24);
    }
    return out;
}

// Pasta montada falsa (como a do motor_package), com um arquivo de nome acentuado e um arquivo grande.
fs::path MakePackage(const fs::path& dir, const std::string& version, size_t bigSize = 300 * 1024) {
    WriteText(dir / "motor-version.json",
              R"({"format": "motor-version", "formatVersion": 1, "version": ")" + version +
                  R"(", "scriptApiVersion": 10, "buildType": "Release", "built": "2026-10-17T17:44:06"})");
    WriteText(dir / "MotorEditor.exe", "editor falso " + version);
    WriteText(dir / "engine" / "shaders" / "pbr.frag", "// shader " + version);
    WriteText(dir / "templates" / PathFromUtf8("Ação Inicial.txt"), "modelo com acento");
    WriteText(dir / "libMotor.dll", Noise(bigSize, 7));
    return dir;
}

bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
} // namespace

int main(int argc, char** argv) {
    // Absoluta: o plano de desinstalação recusa caminhos relativos.
    const fs::path base = fs::absolute(TestDir(argc, argv, "test_distribuicao_tmp")) / PathFromUtf8("Distribuição Ação");
    fs::create_directories(base);

    SECTION("SHA-256 (BCrypt)");
    CHECK(Sha256Of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256Of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    {
        const std::string big = Noise(3 * 1024 * 1024 + 17, 3);
        WriteText(base / "grande.bin", big);
        const auto fromFile = Sha256File(base / "grande.bin");
        CHECK(fromFile && *fromFile == Sha256Of(big));
        Sha256 parts;
        parts.Update(big.data(), 1000);
        parts.Update(big.data() + 1000, big.size() - 1000);
        CHECK(parts.Final() == Sha256Of(big));
        CHECK(!Sha256File(base / "nao-existe.bin"));
    }

    SECTION("Caminhos e URLs");
    CHECK(IsSafeRelativePath("engine/shaders/pbr.frag"));
    CHECK(IsSafeRelativePath("templates/Ação Inicial.txt"));
    CHECK(!IsSafeRelativePath(""));
    CHECK(!IsSafeRelativePath("../fora.txt"));
    CHECK(!IsSafeRelativePath("a/../../b"));
    CHECK(!IsSafeRelativePath("/raiz"));
    CHECK(!IsSafeRelativePath("C:/Windows/x.dll"));
    CHECK(!IsSafeRelativePath("a\\b"));
    CHECK(!IsSafeRelativePath("a//b"));
    CHECK(!IsSafeRelativePath("a/./b"));
    CHECK(!IsSafeRelativePath("nome."));
    CHECK(!IsSafeRelativePath("arquivo.txt:fluxo"));
    CHECK(IsSafeVersionName("0.2.0") && !IsSafeVersionName("0.2/0") && !IsSafeVersionName(".."));
    CHECK(UrlEncodePath("templates/Ação Inicial.txt") == "templates/A%C3%A7%C3%A3o%20Inicial.txt");
    CHECK(UrlEncodePath("a#b%c?d") == "a%23b%25c%3Fd");
    {
        const auto a = ParseServerUrl("http://127.0.0.1:47900/");
        CHECK(a && a->hostPort == "http://127.0.0.1:47900" && a->basePath.empty());
        const auto b = ParseServerUrl("  192.168.0.10:47900/motor/ ");
        CHECK(b && b->hostPort == "http://192.168.0.10:47900" && b->basePath == "/motor");
        const auto c = ParseServerUrl("HTTP://servidor");
        CHECK(c && c->hostPort == "http://servidor:80");
        std::string error;
        CHECK(!ParseServerUrl("https://exemplo.com", &error) && Contains(error, "https"));
        CHECK(!ParseServerUrl("ftp://x"));
        CHECK(!ParseServerUrl(""));
        CHECK(!ParseServerUrl("http://x:99999"));
        CHECK(!ParseServerUrl("http://x:abc"));
        CHECK(NormalizeServerUrl("127.0.0.1:47900/") == "http://127.0.0.1:47900");
        CHECK(NormalizeServerUrl("http://servidor:80") == "http://servidor");
    }
    CHECK(FormatSize(512) == "512 bytes");
    CHECK(FormatSize(25 * 1024 * 1024 + 300 * 1024) == "25,3 MB");
    CHECK(FormatDate("2026-10-17T17:44:06") == "17/10/2026");
    CHECK(HtmlEscape("<a & \"b\">") == "&lt;a &amp; &quot;b&quot;&gt;");

    SECTION("Manifesto: ida e volta e leitura tolerante");
    {
        Manifest m;
        Release r1{"0.2.0", "estável", 10, "Release", "2026-10-17T17:44:06", 0, {}};
        r1.files = {{"MotorEditor.exe", 10, std::string(64, 'a')}, {"engine/x.glsl", 5, std::string(64, 'b')}};
        r1.size = 15;
        Release r2 = r1;
        r2.version = "0.10.0";
        r2.channel = "beta";
        Release r3 = r1;
        r3.version = "0.3.0";
        m.Put(r1);
        m.Put(r2);
        m.Put(r3);
        CHECK(m.releases.size() == 3 && m.releases[0].version == "0.10.0" && m.releases[2].version == "0.2.0");
        CHECK(m.Latest()->version == "0.10.0");
        CHECK(m.Latest("estável")->version == "0.3.0");
        CHECK(!m.Latest("noturno"));
        m.Put(r3);  // a mesma versão substitui
        CHECK(m.releases.size() == 3);
        m.hub = HubRelease{"0.2.0", "2026-10-17", {{"MotorHub.exe", 100, std::string(64, 'c')}}};

        const auto back = Manifest::Parse(m.Dump());
        CHECK(back && back->releases.size() == 3);
        CHECK(back && back->Find("0.2.0") && back->Find("0.2.0")->files.size() == 2 &&
              back->Find("0.2.0")->size == 15 && back->Find("0.2.0")->scriptApiVersion == 10);
        CHECK(back && back->Find("0.10.0")->channel == "beta");
        CHECK(back && back->hub && back->hub->version == "0.2.0" && back->hub->Find("MotorHub.exe"));
        CHECK(m.Remove("0.3.0") && !m.Find("0.3.0") && !m.Remove("0.3.0"));

        // Campos ausentes = padrão; campos novos ignorados; entradas inválidas fora.
        const auto tolerant = Manifest::Parse(R"({
            "format": "motor-releases", "version": 9, "novidade": true,
            "releases": [
                {"version": "1.0.0", "files": [{"path": "a.txt", "size": 1, "sha256": ")" + std::string(64, 'd') + R"("}]},
                {"version": "../1.0.1", "files": [{"path": "a.txt", "size": 1, "sha256": ")" + std::string(64, 'd') + R"("}]},
                {"version": "1.0.2", "files": [{"path": "../fora.txt", "size": 1, "sha256": ")" + std::string(64, 'd') + R"("}]},
                {"version": "1.0.3", "files": [{"path": "b.txt", "size": 1, "sha256": "curto"}]},
                "lixo"
            ]})");
        CHECK(tolerant && tolerant->releases.size() == 1);
        CHECK(tolerant && tolerant->releases[0].channel == kDefaultChannel && tolerant->releases[0].buildType.empty());
        CHECK(tolerant && !tolerant->hub);
        std::string error;
        CHECK(!Manifest::Parse(R"({"format": "outra-coisa"})", &error) && !error.empty());
        CHECK(!Manifest::Parse("não é json", &error));
    }

    SECTION("Pasta de publicações: publicar, regenerar, Hub e remover");
    const fs::path publishRoot = base / PathFromUtf8("Publicações");
    const fs::path pkg1 = MakePackage(base / "montada" / "Motor-0.9.0", "0.9.0");
    const fs::path pkg2 = MakePackage(base / "montada" / "Motor-0.9.1", "0.9.1", 2 * 1024 * 1024 + 5);
    {
        std::string error;
        const auto rel = PublishRelease(pkg1, publishRoot, "", &error);
        CHECK(rel && rel->version == "0.9.0" && rel->files.size() == 5 && rel->channel == kDefaultChannel);
        CHECK(rel && rel->date == "2026-10-17T17:44:06" && rel->buildType == "Release");
        CHECK(fs::exists(ReleaseDir(publishRoot, "0.9.0") / "templates" / PathFromUtf8("Ação Inicial.txt")));
        CHECK(!PublishRelease(pkg1, publishRoot, "", &error) && Contains(error, "já está publicado"));
        CHECK(PublishRelease(pkg2, publishRoot, "beta", &error).has_value());
        const auto loaded = LoadManifest(publishRoot);
        CHECK(loaded && loaded->releases.size() == 2 && loaded->releases[0].version == "0.9.1");
        CHECK(loaded && loaded->Find("0.9.1")->channel == "beta");
        const ReleaseFile* big = nullptr;
        for (const auto& f : loaded->Find("0.9.0")->files) {
            if (f.path == "libMotor.dll") big = &f;
        }
        CHECK(big && big->size == 300 * 1024 && big->sha256 == Sha256Of(Noise(300 * 1024, 7)));

        // Pasta fora do padrão em versoes/ e uma .tmp esquecida: o manifesto regenerado ignora.
        fs::create_directories(publishRoot / "versoes" / "qualquer");
        fs::create_directories(publishRoot / "versoes" / "1.0.0.tmp");
        const auto regen = RegenerateManifest(publishRoot, &error);
        CHECK(regen && regen->releases.size() == 2 && regen->Find("0.9.1")->channel == "beta");

        // Hub e instalador (exes falsos).
        WriteText(base / "hubexe" / "MotorHub.exe", "hub falso");
        WriteText(base / "hubexe" / "MotorSetup.exe", "instalador falso");
        CHECK(!PublishHub(base / "hubexe" / "MotorHub.exe", {}, "versão ruim", publishRoot, &error));
        const auto h = PublishHub(base / "hubexe" / "MotorHub.exe", base / "hubexe" / "MotorSetup.exe", "0.2.0",
                                  publishRoot, &error);
        CHECK(h && h->files.size() == 2 && h->Find(kSetupExe) && h->Find(kSetupExe)->sha256 == Sha256Of("instalador falso"));
        const auto withHub = LoadManifest(publishRoot);
        CHECK(withHub && withHub->hub && withHub->hub->version == "0.2.0" && withHub->releases.size() == 2);

        // Remover: a pasta vai para a "Lixeira" (aqui, uma pasta do teste).
        const fs::path trash = base / "lixeira";
        const auto fakeTrash = [&](const fs::path& p, std::string*) {
            fs::create_directories(trash);
            std::error_code ec;
            fs::rename(p, trash / p.filename(), ec);
            return !ec;
        };
        PublishRelease(MakePackage(base / "montada" / "Motor-0.8.0", "0.8.0"), publishRoot, "", &error);
        CHECK(UnpublishRelease(publishRoot, "0.8.0", fakeTrash, &error));
        CHECK(fs::exists(trash / "0.8.0") && !fs::exists(ReleaseDir(publishRoot, "0.8.0")));
        CHECK(!LoadManifest(publishRoot)->Find("0.8.0") && LoadManifest(publishRoot)->hub);
        CHECK(!UnpublishRelease(publishRoot, "0.8.0", fakeTrash, &error) && Contains(error, "não está publicado"));
        CHECK(!UnpublishRelease(publishRoot, "..", fakeTrash, &error));
    }

    SECTION("Plano de instalação e de desinstalação");
    {
        const fs::path installDir = base / "Programs" / "Motor Hub";
        const fs::path dataDir = base / PathFromUtf8("Dados Ação");
        InstallOptions o;
        o.installDir = installDir;
        o.dataDir = dataDir;
        o.desktopShortcut = true;
        o.hubVersion = "0.2.0";
        o.installedBytes = 10 * 1024 * 1024;
        o.server = "http://127.0.0.1:47900";
        const InstallPlan plan = PlanInstall(o, base / "Menu", base / "Desktop");
        CHECK(plan.hubExe == installDir / "MotorHub.exe" && plan.setupExe == installDir / "MotorSetup.exe");
        CHECK(plan.shortcuts.size() == 2 && plan.shortcuts[0].link == base / "Menu" / "Motor Hub.lnk");
        CHECK(plan.shortcuts[1].link == base / "Desktop" / "Motor Hub.lnk");
        CHECK(plan.shortcuts[0].target == plan.hubExe);
        CHECK(plan.shortcuts[0].arguments == "--data \"" + ToUtf8(dataDir) + "\"");
        CHECK(plan.registryKey == kUninstallKey);
        bool uninstallString = false, size = false;
        for (const RegistryValue& v : plan.registryValues) {
            if (v.name == "UninstallString") uninstallString = Contains(v.text, "MotorSetup.exe\" --desinstalar");
            if (v.name == "EstimatedSize") size = v.isNumber && v.number == 10 * 1024;
        }
        CHECK(uninstallString && size);
        CHECK(plan.record.shortcuts.size() == 2 && plan.record.registryKey == kUninstallKey);
        CHECK(plan.record.servers.size() == 1);

        // Sem atalhos, sem registro, pasta de dados padrão.
        InstallOptions quiet = o;
        quiet.startMenuShortcut = quiet.desktopShortcut = quiet.registerUninstall = false;
        quiet.dataDir.clear();
        const InstallPlan q = PlanInstall(quiet, base / "Menu", base / "Desktop");
        CHECK(q.shortcuts.empty() && q.registryKey.empty() && q.registryValues.empty() && q.record.dataDir.empty());
        CHECK(HubArguments({}).empty());
        CHECK(HubArguments(PathFromUtf8("C:\\Dados\\")) == "--data \"C:\\Dados\"");

        // instalacao.json: ida e volta.
        CHECK(plan.record.Save(plan.recordFile));
        const auto record = InstallRecord::Load(plan.recordFile);
        CHECK(record && record->dataDir == dataDir && record->installDir == installDir.lexically_normal());
        CHECK(record && record->shortcuts.size() == 2 && record->hubVersion == "0.2.0" && record->servers.size() == 1);
        CHECK(!InstallRecord::Load(base / "nao-existe.json"));

        // Desinstalar sem os dados: a pasta do hub (tem o instalacao.json) e os atalhos.
        fs::create_directories(dataDir / PathFromUtf8("Versões") / "0.2.0");
        WriteText(dataDir / "hub.json", "{}");
        WriteText(dataDir / "outro.txt", "não é do hub");
        UninstallPlan u = PlanUninstall(*record, false, base / "padrao");
        CHECK(u.toTrash.size() == 1 && u.toTrash[0] == installDir.lexically_normal());
        CHECK(u.shortcuts.size() == 2 && u.registryKey == kUninstallKey && u.emptyDirToRemove.empty());
        // Com os dados: só Versões\ e hub.json (outro.txt fica).
        u = PlanUninstall(*record, true, base / "padrao");
        CHECK(u.toTrash.size() == 3 && u.toTrash[1] == (dataDir / PathFromUtf8("Versões")).lexically_normal() &&
              u.toTrash[2] == (dataDir / "hub.json").lexically_normal());
        CHECK(u.emptyDirToRemove == dataDir.lexically_normal());

        // Registro estragado nunca leva uma raiz nem uma pasta que não é do hub.
        InstallRecord bad;
        bad.installDir = base.root_path();
        bad.dataDir = base.root_path();
        bad.shortcuts = {base / "nao-e-atalho.txt"};
        u = PlanUninstall(bad, true, {});
        CHECK(u.toTrash.empty() && u.shortcuts.empty() && u.emptyDirToRemove.empty());
        bad.installDir = base / "pasta-qualquer";
        fs::create_directories(bad.installDir);
        CHECK(PlanUninstall(bad, false, {}).toTrash.empty());

        // Ferramentas para compilar scripts: pelo MotorSDK.json da versão, ou o MSYS2 padrão.
        WriteText(base / "sdk" / "fake-g++.exe", "x");
        {
            // No JSON as barras invertidas vão escapadas.
            std::string cxx = ToUtf8(base / "sdk" / "fake-g++.exe");
            std::string escaped;
            for (char c : cxx) escaped += c == '\\' ? std::string("\\\\") : std::string(1, c);
            WriteText(base / "sdk" / "MotorSDK.json",
                      R"({"cxxCompiler": ")" + escaped + R"(", "cmake": "C:/nao/existe/cmake.exe"})");
        }
        const auto tools = CheckBuildTools(base / "sdk" / "MotorSDK.json");
        CHECK(tools.size() == 2 && tools[0].found && !tools[1].found);
        const auto none = CheckBuildTools(base / "sem-sdk.json", base / "msys-falso", {});
        CHECK(none.size() == 2 && !none[0].found && !none[1].found &&
              none[0].path == (base / "msys-falso" / "ucrt64" / "bin" / "g++.exe").make_preferred());
        // Sem SDK, o CMake fora do MSYS2 (Arquivos de Programas, PATH) também vale.
        WriteText(base / "outro-cmake" / "cmake.exe", "x");
        const auto other = CheckBuildTools(base / "sem-sdk.json", base / "msys-falso", {base / "outro-cmake" / "cmake.exe"});
        CHECK(other[1].found && other[1].path == (base / "outro-cmake" / "cmake.exe").make_preferred());
    }

    SECTION("hub.json formato 2: servidores");
    {
        WriteText(base / "hub-v1.json", R"({"format": "motor-hub", "version": 1, "projects": []})");
        hub::HubConfig v1 = hub::HubConfig::Load(base / "hub-v1.json");
        CHECK(v1.servers.size() == 1 && v1.servers[0] == kDefaultServer);  // formato 1: o servidor local
        v1.servers = {"http://192.168.0.10:47900", "http://127.0.0.1:47900"};
        CHECK(v1.Save(base / "hub-v2.json"));
        const hub::HubConfig v2 = hub::HubConfig::Load(base / "hub-v2.json");
        CHECK(v2.servers.size() == 2 && v2.servers[0] == "http://192.168.0.10:47900");
        CHECK(Contains(ReadText(base / "hub-v2.json"), "\"version\": 2"));
        WriteText(base / "hub-vazio.json", R"({"format": "motor-hub", "version": 2, "servers": []})");
        CHECK(hub::HubConfig::Load(base / "hub-vazio.json").servers.empty());  // o usuário tirou todos
        CHECK(hub::HubConfig::Load(base / "nao-existe.json").servers.size() == 1);
        hub::HubConfig c;
        c.servers.clear();
        CHECK(c.AddServer("127.0.0.1:47900/") && !c.AddServer("http://127.0.0.1:47900"));
        CHECK(c.servers.size() == 1 && c.servers[0] == "http://127.0.0.1:47900");
    }

    SECTION("Servidor e cliente em loopback");
    {
        DistribServer server(publishRoot);
        std::string error;
        const int port = server.Start("127.0.0.1", 0, &error);
        CHECK(port > 0);
        const std::string url = "http://127.0.0.1:" + std::to_string(port);

        Client client(url);
        const auto manifest = client.FetchManifest(&error);
        CHECK(manifest && manifest->releases.size() == 2 && manifest->hub);
        const auto page = client.GetText("/");
        CHECK(page && Contains(*page, "Motor 0.9.1") && Contains(*page, "/hub/MotorSetup.exe"));
        const auto hubRelease = client.FetchHubRelease(&error);
        CHECK(hubRelease && hubRelease->version == "0.2.0");

        // Range direto: só o trecho pedido, com 206.
        httplib::Client raw("127.0.0.1", port);
        raw.set_path_encode(false);
        const std::string bigUrl = FileUrl("0.9.0", "libMotor.dll");
        const auto part = raw.Get(bigUrl, httplib::Headers{{"Range", "bytes=100-199"}});
        CHECK(part && part->status == 206 && part->body == Noise(300 * 1024, 7).substr(100, 100));
        const auto accented = raw.Get(FileUrl("0.9.0", "templates/Ação Inicial.txt"));
        CHECK(accented && accented->status == 200 && accented->body == "modelo com acento");
        // Caminhos que saem da pasta ou não estão no manifesto.
        const auto escape = raw.Get("/arquivos/0.9.0/%2E%2E/%2E%2E/manifesto.json");
        CHECK(escape && escape->status == 400);
        const auto notListed = raw.Get("/arquivos/0.9.0/nao-esta.txt");
        CHECK(notListed && notListed->status == 404);
        const auto badVersion = raw.Get("/arquivos/0.7.0/MotorEditor.exe");
        CHECK(badVersion && badVersion->status == 404);

        // Uma versão inteira: instala em Versões\0.9.0 de uma vez.
        const fs::path versions = base / PathFromUtf8("Dados Cliente") / PathFromUtf8("Versões");
        int calls = 0;
        uint64_t lastDone = 0;
        bool monotonic = true;
        const ReleaseOutcome first = DownloadRelease(url, *manifest->Find("0.9.0"), versions, nullptr,
                                                     [&](const ReleaseProgress& p) {
                                                         ++calls;
                                                         if (p.done < lastDone) monotonic = false;
                                                         lastDone = p.done;
                                                     });
        CHECK(first.status == Status::Ok && first.dir == versions / "0.9.0");
        CHECK(first.received == manifest->Find("0.9.0")->size && first.reused == 0);
        CHECK(calls > 0 && monotonic && lastDone == manifest->Find("0.9.0")->size);
        CHECK(ReadText(versions / "0.9.0" / "libMotor.dll") == Noise(300 * 1024, 7));
        CHECK(ReadText(versions / "0.9.0" / "templates" / PathFromUtf8("Ação Inicial.txt")) == "modelo com acento");
        CHECK(hub::ReadInstalledVersion(versions / "0.9.0").has_value());
        CHECK(!fs::exists(StagingRoot(versions)));
        const auto scanned = hub::ScanVersions(versions);
        CHECK(scanned.size() == 1 && scanned[0].version == "0.9.0");
        const ReleaseOutcome again = DownloadRelease(url, *manifest->Find("0.9.0"), versions);
        CHECK(again.status == Status::Failed && Contains(again.error, "já está instalado"));

        // Cancelar no meio e retomar: só o que falta vem pela rede.
        const Release& r2 = *manifest->Find("0.9.1");
        std::atomic<bool> cancel{false};
        const ReleaseOutcome cancelled = DownloadRelease(url, r2, versions, &cancel, [&](const ReleaseProgress& p) {
            if (p.currentFile == "libMotor.dll" && p.done > 600 * 1024) cancel = true;
        });
        CHECK(cancelled.status == Status::Cancelled);
        CHECK(!fs::exists(versions / "0.9.1") && fs::is_directory(StagingDir(versions, "0.9.1")));
        const uint64_t staged = StagedBytes(versions, r2);
        CHECK(staged > 600 * 1024 && staged < r2.size);
        CHECK(hub::ScanVersions(versions).size() == 1);  // a parcial não aparece como instalada
        const ReleaseOutcome resumed = DownloadRelease(url, r2, versions);
        CHECK(resumed.status == Status::Ok);
        CHECK(resumed.received + resumed.reused == r2.size && resumed.received < r2.size - 500 * 1024);
        CHECK(Sha256File(versions / "0.9.1" / "libMotor.dll") == std::optional<std::string>(Sha256Of(Noise(2 * 1024 * 1024 + 5, 7))));
        CHECK(hub::ScanVersions(versions).size() == 2);

        // Pedaço corrompido: o SHA-256 não confere, baixa de novo do zero e fica certo.
        const ReleaseFile* bigFile = nullptr;
        for (const auto& f : r2.files) {
            if (f.path == "libMotor.dll") bigFile = &f;
        }
        const fs::path corrupt = base / "corrompido" / "libMotor.dll";
        WriteText(corrupt, std::string(1000, 'x'));
        const Client::FileResult fixed = client.DownloadFile(FileUrl("0.9.1", "libMotor.dll"), corrupt, *bigFile);
        CHECK(fixed.status == Status::Ok && Sha256File(corrupt) == std::optional<std::string>(bigFile->sha256));
        // Completo e certo: não baixa nada.
        const Client::FileResult cached = client.DownloadFile(FileUrl("0.9.1", "libMotor.dll"), corrupt, *bigFile);
        CHECK(cached.status == Status::Ok && cached.received == 0);
        // Manifesto mentindo o hash: falha e não deixa o arquivo.
        ReleaseFile lie = *bigFile;
        lie.sha256 = std::string(64, '0');
        const Client::FileResult lied = client.DownloadFile(FileUrl("0.9.1", "libMotor.dll"), base / "mentira.dll", lie);
        CHECK(lied.status == Status::Failed && Contains(lied.error, "SHA-256") && !fs::exists(base / "mentira.dll"));

        // O instalador publicado.
        const Client::FileResult setup = client.DownloadFile(HubFileUrl(kSetupExe), base / "baixado" / kSetupExe,
                                                             *hubRelease->Find(kSetupExe));
        CHECK(setup.status == Status::Ok && ReadText(base / "baixado" / kSetupExe) == "instalador falso");

        // O manifesto muda no disco (outra publicação): o servidor relê sem reiniciar.
        CHECK(PublishRelease(MakePackage(base / "montada" / "Motor-0.9.2", "0.9.2"), publishRoot, "", &error));
        const auto after = client.FetchManifest(&error);
        CHECK(after && after->releases.size() == 3 && after->releases[0].version == "0.9.2");

        server.Stop();
        Client down(url);
        CHECK(!down.FetchManifest(&error) && Contains(error, "Sem resposta"));
        Client invalid("https://x");
        CHECK(!invalid.Valid() && !invalid.FetchManifest(&error));
    }

    return TestResult();
}
