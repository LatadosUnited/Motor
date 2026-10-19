// Identidade do jogo exportado (Fase 21): versão no .mproject, leitura de ícones .png e .ico, o bloco de versão
// do Windows e a gravação dos dois num .exe de verdade (uma cópia deste teste), lidos de volta pelas APIs do
// Windows; e a exportação com o nome do .exe, o ícone e a versão do projeto.
#include "TestUtil.h"

#include "export/ExeResources.h"
#include "export/GameExport.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/core/Paths.h>
#include <motor/project/Project.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace editor;
using namespace motor;
namespace fs = std::filesystem;

namespace {

void WriteBytes(const fs::path& file, const std::vector<uint8_t>& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                               static_cast<std::streamsize>(bytes.size()));
}

// Só o começo de um PNG (assinatura e IHDR): o bastante para o ReadIcon, que não decodifica a imagem.
std::vector<uint8_t> FakePng(uint32_t w, uint32_t h) {
    std::vector<uint8_t> b = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    for (uint32_t v : {w, h}) {
        for (int shift = 24; shift >= 0; shift -= 8) b.push_back(static_cast<uint8_t>(v >> shift));
    }
    for (int i = 0; i < 9; ++i) b.push_back(0);
    return b;
}

// .ico com duas imagens: 16x16 (um DIB de mentira de 32 bits) e 256x256 (PNG).
std::vector<uint8_t> FakeIco() {
    std::vector<uint8_t> dib(40 + 16 * 16 * 4 + 16 * 4, 0);
    dib[0] = 40;
    dib[14] = 32;  // biBitCount
    const std::vector<uint8_t> png = FakePng(256, 256);
    std::vector<uint8_t> b = {0, 0, 1, 0, 2, 0};
    const auto entry = [&](uint8_t size, uint16_t bits, size_t bytes, size_t offset) {
        b.insert(b.end(), {size, size, 0, 0, 1, 0, static_cast<uint8_t>(bits), 0});
        for (size_t v : {bytes, offset}) {
            for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }
    };
    entry(16, 32, dib.size(), 6 + 32);
    entry(0, 32, png.size(), 6 + 32 + dib.size());
    b.insert(b.end(), dib.begin(), dib.end());
    b.insert(b.end(), png.begin(), png.end());
    return b;
}

#ifdef _WIN32
fs::path ThisExe() {
    wchar_t buffer[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return fs::path(std::wstring(buffer, n));
}

std::wstring VersionString(const fs::path& exe, const wchar_t* key) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &ignored);
    if (!size) return {};
    std::vector<uint8_t> data(size);
    if (!GetFileVersionInfoW(exe.c_str(), 0, size, data.data())) return {};
    void* value = nullptr;
    UINT length = 0;
    const std::wstring path = std::wstring(L"\\StringFileInfo\\041604B0\\") + key;
    if (!VerQueryValueW(data.data(), path.c_str(), &value, &length) || !value) return {};
    return std::wstring(static_cast<const wchar_t*>(value));
}

uint64_t FixedVersion(const fs::path& exe) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &ignored);
    std::vector<uint8_t> data(size ? size : 1);
    if (!size || !GetFileVersionInfoW(exe.c_str(), 0, size, data.data())) return 0;
    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fixed), &length) || !fixed) return 0;
    return (static_cast<uint64_t>(fixed->dwFileVersionMS) << 32) | fixed->dwFileVersionLS;
}

// Tamanhos (largura) das imagens do grupo GLFW_ICON gravado no .exe; vazio se não houver.
std::vector<int> IconGroup(const fs::path& exe) {
    std::vector<int> sizes;
    HMODULE module = LoadLibraryExW(exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module) return sizes;
    if (HRSRC res = FindResourceW(module, L"GLFW_ICON", MAKEINTRESOURCEW(14))) {
        const auto* bytes = static_cast<const uint8_t*>(LockResource(LoadResource(module, res)));
        const int count = bytes[4] | (bytes[5] << 8);
        for (int i = 0; i < count; ++i) {
            const int w = bytes[6 + i * 14];
            sizes.push_back(w == 0 ? 256 : w);
            const int id = bytes[6 + i * 14 + 12] | (bytes[6 + i * 14 + 13] << 8);
            if (!FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(3))) sizes.push_back(-1);
        }
    }
    FreeLibrary(module);
    return sizes;
}
#endif

} // namespace

int main(int argc, char** argv) {
    const fs::path dir = TestDir(argc, argv, "test_exe_identity_tmp");

    SECTION("Versão do jogo");
    {
        int v[4];
        CHECK(ParseGameVersion("1.2.3", v) && v[0] == 1 && v[1] == 2 && v[2] == 3 && v[3] == 0);
        CHECK(ParseGameVersion("10", v) && v[0] == 10 && v[1] == 0);
        CHECK(ParseGameVersion("1.0.0.7", v) && v[3] == 7);
        CHECK(!ParseGameVersion("1.0 beta", v));
        CHECK(!ParseGameVersion("1..2", v));
        CHECK(!ParseGameVersion("1.2.3.4.5", v));
        CHECK(!ParseGameVersion("", v));
    }

    SECTION("Identidade no .mproject e nome do .exe");
    {
        std::string error;
        auto project = Project::Create(dir, "Jogo Ação", &error);
        CHECK(project.has_value());
        if (!project) return TestResult();
        CHECK(project->Identity() == GameIdentity{});
        CHECK(project->ExecutableName() == "Jogo Ação");
        GameIdentity id;
        id.executable = "Aventura Ação";
        id.version = "2.1.0";
        id.icon = "Assets/Icones/jogo.ico";
        project->SetIdentity(id);
        CHECK(project->ExecutableName() == "Aventura Ação");
        CHECK(project->Save(&error));
        auto reopened = Project::Open(project->RootDir(), &error);
        CHECK(reopened && reopened->Identity() == id);
        id.executable = "nome/inválido";
        project->SetIdentity(id);
        CHECK(project->ExecutableName() == "Jogo Ação");  // nome que não serve de arquivo: o do projeto
    }

    SECTION("Ícones .png e .ico");
    {
        std::vector<ExeResources::IconImage> images;
        std::string error;
        WriteBytes(dir / "icone.png", FakePng(256, 256));
        CHECK(ExeResources::ReadIcon(dir / "icone.png", images, error));
        CHECK(images.size() == 1 && images[0].width == 256 && images[0].bitCount == 32);
        WriteBytes(dir / "grande.png", FakePng(512, 512));
        CHECK(!ExeResources::ReadIcon(dir / "grande.png", images, error) && error.find("256") != std::string::npos);
        WriteBytes(dir / "icone.ico", FakeIco());
        CHECK(ExeResources::ReadIcon(dir / "icone.ico", images, error));
        CHECK(images.size() == 2 && images[0].width == 16 && images[1].width == 256);
        WriteBytes(dir / "texto.ico", {'o', 'i'});
        CHECK(!ExeResources::ReadIcon(dir / "texto.ico", images, error));
        CHECK(!ExeResources::ReadIcon(dir / "nao-existe.png", images, error));
    }

#ifdef _WIN32
    SECTION("Gravar no .exe e ler pelo Windows");
    {
        const fs::path exe = dir / PathFromUtf8("Cópia Ação.exe");
        std::error_code ec;
        fs::copy_file(ThisExe(), exe, fs::copy_options::overwrite_existing, ec);
        CHECK(!ec);
        std::vector<ExeResources::IconImage> icon;
        std::string error;
        CHECK(ExeResources::ReadIcon(dir / "icone.ico", icon, error));
        CHECK(ExeResources::Apply(exe, icon, {"Jogo Ação", "2.1.3", "Cópia Ação.exe"}, error));
        if (!error.empty()) std::printf("  erro: %s\n", error.c_str());
        CHECK(VersionString(exe, L"ProductName") == L"Jogo Ação");
        CHECK(VersionString(exe, L"FileVersion") == L"2.1.3");
        CHECK(VersionString(exe, L"OriginalFilename") == L"Cópia Ação.exe");
        CHECK(FixedVersion(exe) == ((2ull << 48) | (1ull << 32) | (3ull << 16)));
        CHECK(IconGroup(exe) == (std::vector<int>{16, 256}));
    }

    SECTION("Exportar com nome, ícone e versão do projeto");
    {
        std::string error;
        auto project = Project::Open(dir / PathFromUtf8("Jogo Ação"), &error);
        CHECK(project.has_value());
        if (!project) return TestResult();
        const fs::path scene = project->AssetsDir() / "Scenes" / "Inicio.mscene";
        WriteBytes(scene, {'{', '}'});
        project->SetStartupScene(scene);
        WriteBytes(project->RootDir() / "Assets" / "Icones" / "jogo.png", FakePng(64, 64));
        GameIdentity id;
        id.executable = "Aventura";
        id.version = "0.9";
        id.icon = "Assets/Icones/jogo.png";
        project->SetIdentity(id);

        const fs::path fake = dir / "falso";
        for (const char* dll : GameExport::kRuntimeDlls) WriteBytes(fake / dll, {'M', 'Z'});
        WriteBytes(fake / "engine" / "shaders" / "lit.vert", {'#'});
        WriteBytes(fake / "engine" / "fonts" / "f.ttf", {'f'});
        AssetLibrary assets;
        assets.SetRoot(project->RootDir());
        AssetDatabase& db = assets.Database();
        GameExport::Options options;
        options.outputDir = dir / PathFromUtf8("Saída");
        options.playerExe = ThisExe();  // um .exe de verdade no lugar do Player
        options.runtimeDir = fake;
        options.engineAssetsDir = fake / "engine";
        GameExport::Result r = GameExport::Export(*project, db, options);
        if (!r.ok) std::printf("  erro: %s\n", r.error.c_str());
        CHECK(r.ok);
        CHECK(r.executable == options.outputDir / "Aventura.exe");
        for (const std::string& w : r.warnings) std::printf("  aviso: %s\n", w.c_str());
        CHECK(r.warnings.empty());
        CHECK(VersionString(r.executable, L"ProductName") == L"Jogo Ação");
        CHECK(VersionString(r.executable, L"ProductVersion") == L"0.9");
        CHECK(IconGroup(r.executable) == std::vector<int>{64});

        // Ícone que não serve: a exportação é recusada antes de mexer na pasta.
        WriteBytes(project->RootDir() / "Assets" / "Icones" / "jogo.png", FakePng(300, 300));
        r = GameExport::Export(*project, db, options);
        CHECK(!r.ok && r.error.find("Ícone") != std::string::npos);
        CHECK(fs::exists(options.outputDir / "Aventura.exe"));
    }
#endif

    return TestResult();
}
