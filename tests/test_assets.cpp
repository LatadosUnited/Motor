// Carregadores (OBJ, imagem) e caminhos do AssetLibrary. Não cria contexto OpenGL.
#include "TestUtil.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/assets/AssetLoaders.h>
#include <motor/core/Paths.h>

#include <glm/geometric.hpp>

#include <cstdint>
#include <fstream>

namespace fs = std::filesystem;
using namespace motor;

namespace {
// PNG RGBA 2x2: linha de cima vermelho | verde, linha de baixo azul | branco com alfa 128.
const uint8_t kPng2x2[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00, 0x00, 0x00, 0x13, 0x49,
    0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x0c, 0x81, 0x34, 0x08, 0x34, 0x00, 0x00, 0x49,
    0x49, 0x09, 0x78, 0x28, 0xa0, 0xdb, 0x77, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};
} // namespace

int main(int argc, char** argv) {
    const fs::path base = TestDir(argc, argv, "test_assets_tmp");
    std::string error;

    SECTION("OBJ: quad com vt/vn");
    MeshData m;
    CHECK(ParseObj("# quad\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
                   "f 1/1/1 2/2/1 3/3/1 4/4/1\n", m, &error));
    CHECK(m.vertices.size() == 4);
    CHECK(m.indices.size() == 6);
    CHECK(m.vertices[2].uv == glm::vec2(1, 1));
    CHECK(m.vertices[0].normal == glm::vec3(0, 0, 1));

    SECTION("OBJ: sem normais, índices negativos, CRLF");
    CHECK(ParseObj("v 0 0 0\r\nv 1 0 0\r\nv 0 0 -1\r\nf -3 -2 -1\r\n", m, &error));
    CHECK(m.indices.size() == 3);
    CHECK(glm::length(m.vertices[0].normal - glm::vec3(0, 1, 0)) < 1e-5f);

    SECTION("OBJ: v//n e vértices compartilhados");
    CHECK(ParseObj("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//1 3//1\nf 1//1 3//1 4//1\n", m, &error));
    CHECK(m.vertices.size() == 4);
    CHECK(m.indices.size() == 6);

    SECTION("OBJ: erros");
    CHECK(!ParseObj("v 0 0 0\nf 1 2 3\n", m, &error));
    CHECK(!ParseObj("v 0 0 0\n", m, &error));
    CHECK(!ParseObj("v a b c\n", m, &error));

    SECTION("Imagem PNG 2x2");
    const fs::path png = base / "t.png";
    std::ofstream(png, std::ios::binary).write(reinterpret_cast<const char*>(kPng2x2), sizeof(kPng2x2));
    ImageData img;
    CHECK(LoadImageFile(png, img, &error));
    CHECK(img.width == 2 && img.height == 2 && img.pixels.size() == 16);
    // Linha 0 = base da imagem: o pixel (0,0) é o azul de baixo à esquerda.
    CHECK(img.pixels.size() == 16 && img.pixels[0] == 0 && img.pixels[1] == 0 && img.pixels[2] == 255);
    CHECK(img.pixels.size() == 16 && img.pixels[7] == 128);
    CHECK(img.pixels.size() == 16 && img.pixels[8] == 255 && img.pixels[9] == 0);
    const uint8_t junk[] = {1, 2, 3, 4};
    CHECK(!DecodeImage(junk, img, &error));

    SECTION("Tipos");
    CHECK(AssetTypeFromPath("a/B.PNG") == AssetType::Texture);
    CHECK(AssetTypeFromPath("x.jpeg") == AssetType::Texture);
    CHECK(AssetTypeFromPath("x.Obj") == AssetType::Mesh);
    CHECK(AssetTypeFromPath("x.mscene") == AssetType::Scene);
    CHECK(AssetTypeFromPath("x.txt") == AssetType::Unknown);

    SECTION("AssetLibrary: caminhos");
    const fs::path proj = base / "proj";
    fs::create_directories(proj / "Assets" / PathFromUtf8("Modelos Ação"));
    fs::create_directories(proj / "Assets" / ".oculto");
    fs::copy_file(png, proj / "Assets" / "tex.png");
    std::ofstream(proj / "Assets" / PathFromUtf8("Modelos Ação") / "cubo.obj") << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    std::ofstream(proj / "Assets" / ".oculto" / "x.obj") << "v 0 0 0\n";
    AssetLibrary lib;
    lib.SetRoot(proj);
    CHECK(lib.KeyForFile(proj / "Assets" / "tex.png") == "Assets/tex.png");
    CHECK(lib.KeyForFile(base / "fora.png").empty());
    CHECK(lib.FileForKey("Assets/tex.png") == (proj / "Assets" / "tex.png").lexically_normal());
    CHECK(lib.FileForKey("Cubo").empty());
    CHECK(lib.FileForKey("../fora.png").empty());
    CHECK(AssetLibrary::IsFileKey("Assets/a.obj") && !AssetLibrary::IsFileKey("Xadrez"));
    const auto meshes = lib.ScanFiles(AssetType::Mesh);
    CHECK(meshes.size() == 1 && meshes[0] == "Assets/Modelos Ação/cubo.obj");
    CHECK(lib.ScanFiles(AssetType::Texture).size() == 1);
    CHECK(!lib.GetMesh("Assets/nao-existe.obj", &error));
    CHECK(lib.Database().Count() == 2);  // SetRoot já criou os .meta

    return TestResult();
}
