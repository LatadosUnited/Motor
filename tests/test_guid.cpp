// AssetDatabase: GUIDs em arquivos .meta. Não usa OpenGL.
#include "TestUtil.h"

#include <motor/assets/AssetDatabase.h>
#include <motor/core/Paths.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>

namespace fs = std::filesystem;
using namespace motor;

static void Touch(const fs::path& p, const char* text = "x") {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}

static std::string ReadAll(const fs::path& p) {
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    const fs::path base = TestDir(argc, argv, "test_guid_tmp");
    const fs::path assets = base / "Assets";
    std::error_code ec;
    fs::remove_all(base, ec);

    Touch(assets / "a.png");
    Touch(assets / PathFromUtf8("Modelos Ação") / "b.obj");
    Touch(assets / "Scenes" / "c.mscene");
    Touch(assets / "leia.txt");
    Touch(assets / ".oculto" / "d.png");

    SECTION("Refresh cria .meta só para assets");
    AssetDatabase db;
    db.SetAssetsDir(assets);
    CHECK(db.Count() == 3);
    CHECK(fs::exists(assets / "a.png.meta"));
    CHECK(fs::exists(assets / PathFromUtf8("Modelos Ação") / "b.obj.meta"));
    CHECK(fs::exists(assets / "Scenes" / "c.mscene.meta"));
    CHECK(!fs::exists(assets / "leia.txt.meta"));
    CHECK(!fs::exists(assets / ".oculto" / "d.png.meta"));
    const std::string guidA = db.FindGuid(assets / "a.png");
    const std::string guidB = db.FindGuid(assets / PathFromUtf8("Modelos Ação") / "b.obj");
    CHECK(AssetDatabase::IsValidGuid(guidA));
    CHECK(guidA != guidB);
    CHECK(ReadAll(assets / "a.png.meta").find(guidA) != std::string::npos);

    SECTION("GUID estável entre sessões");
    {
        AssetDatabase other;
        other.SetAssetsDir(assets);
        CHECK(other.FindGuid(assets / "a.png") == guidA);
        CHECK(other.PathFor(guidB) == (assets / PathFromUtf8("Modelos Ação") / "b.obj").lexically_normal());
    }

    SECTION("Renomear arquivo + .meta mantém o GUID");
    fs::rename(assets / "a.png", assets / "tijolo.png");
    fs::rename(assets / "a.png.meta", assets / "tijolo.png.meta");
    db.Refresh();
    CHECK(db.PathFor(guidA) == (assets / "tijolo.png").lexically_normal());
    CHECK(db.FindGuid(assets / "a.png").empty());

    SECTION("Mover pasta inteira mantém os GUIDs de dentro");
    fs::rename(assets / PathFromUtf8("Modelos Ação"), assets / "Modelos");
    db.Refresh();
    CHECK(db.PathFor(guidB) == (assets / "Modelos" / "b.obj").lexically_normal());

    SECTION("Cópia com .meta repetido: o original fica com o GUID");
    fs::copy_file(assets / "tijolo.png", assets / "aaa copia.png");  // ordem alfabética antes do original
    fs::copy_file(assets / "tijolo.png.meta", assets / "aaa copia.png.meta");
    db.Refresh();
    CHECK(db.FindGuid(assets / "tijolo.png") == guidA);
    const std::string guidCopy = db.FindGuid(assets / "aaa copia.png");
    CHECK(AssetDatabase::IsValidGuid(guidCopy) && guidCopy != guidA);
    CHECK(ReadAll(assets / "aaa copia.png.meta").find(guidCopy) != std::string::npos);
    CHECK(db.Count() == 4);

    SECTION("Banco novo com repetidos: o primeiro em ordem fica");
    fs::copy_file(assets / "Modelos" / "b.obj", assets / "Modelos" / "z.obj");
    fs::copy_file(assets / "Modelos" / "b.obj.meta", assets / "Modelos" / "z.obj.meta");
    {
        AssetDatabase fresh;
        fresh.SetAssetsDir(assets);
        CHECK(fresh.FindGuid(assets / "Modelos" / "b.obj") == guidB);
        CHECK(fresh.FindGuid(assets / "Modelos" / "z.obj") != guidB);
    }

    SECTION(".meta inválido é regenerado");
    Touch(assets / "e.png");
    Touch(assets / "e.png.meta", "{ lixo");
    db.Refresh();
    CHECK(AssetDatabase::IsValidGuid(db.FindGuid(assets / "e.png")));

    SECTION("GuidFor cria o .meta na hora");
    Touch(assets / "novo.obj");
    const std::string guidNew = db.GuidFor(assets / "novo.obj");
    CHECK(AssetDatabase::IsValidGuid(guidNew));
    CHECK(fs::exists(assets / "novo.obj.meta"));
    CHECK(db.GuidFor(base / "fora.png").empty());
    CHECK(db.GuidFor(assets / "leia.txt").empty());

    SECTION("Mover sem o .meta perde o GUID (limitação esperada)");
    fs::rename(assets / "novo.obj", assets / "Modelos" / "novo.obj");
    db.Refresh();
    CHECK(db.PathFor(guidNew).empty());
    CHECK(!db.FindGuid(assets / "Modelos" / "novo.obj").empty());

    SECTION("GUIDs únicos");
    std::set<std::string> guids;
    for (int i = 0; i < 1000; ++i) guids.insert(AssetDatabase::NewGuid());
    CHECK(guids.size() == 1000);
    CHECK(AssetDatabase::IsMetaFile("x.png.meta") && !AssetDatabase::IsMetaFile("x.png"));

    fs::remove_all(base, ec);
    return TestResult();
}
