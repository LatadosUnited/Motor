// Texturas carregadas em paralelo (Fase 26): AssetLibrary::PreloadTextures com texturas e materiais, arquivos que
// não abrem e a cena que já chega com as texturas carregadas. Sem janela: a fábrica de texturas não usa a GPU.
#include "TestUtil.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/renderer/Material.h>
#include <motor/renderer/Mesh.h>
#include <motor/renderer/Texture.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneSerializer.h>

#include <cstdint>
#include <fstream>
#include <format>
#include <thread>
#include <vector>

using namespace motor;
namespace fs = std::filesystem;

namespace {
char g_fakeTexture;

// BMP de 24 bits, uma cor só.
void WriteBmp(const fs::path& file, int size, uint8_t r, uint8_t g, uint8_t b) {
    const int row = (size * 3 + 3) / 4 * 4;
    const uint32_t pixelBytes = static_cast<uint32_t>(row * size);
    std::vector<uint8_t> data(54 + pixelBytes, 0);
    auto put32 = [&](size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i) data[at + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
    };
    data[0] = 'B';
    data[1] = 'M';
    put32(2, 54 + pixelBytes);
    put32(10, 54);
    put32(14, 40);
    put32(18, static_cast<uint32_t>(size));
    put32(22, static_cast<uint32_t>(size));
    data[26] = 1;
    data[28] = 24;
    put32(34, pixelBytes);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint8_t* p = &data[54 + static_cast<size_t>(y * row + x * 3)];
            p[0] = b;
            p[1] = g;
            p[2] = r;
        }
    }
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

std::shared_ptr<Texture2D> FakeTexture() {
    return std::shared_ptr<Texture2D>(reinterpret_cast<Texture2D*>(&g_fakeTexture), [](Texture2D*) {});
}
} // namespace

int main(int argc, char** argv) {
    const fs::path root = TestDir(argc, argv, "test_asset_preload_tmp");
    const fs::path folder = root / "Assets" / "Texturas";
    fs::create_directories(folder);
    std::vector<std::string> guids;
    {
        AssetLibrary setup;
        setup.SetRoot(root);
        for (int i = 0; i < 6; ++i) {
            const fs::path file = folder / std::format("cor{}.bmp", i);
            WriteBmp(file, 32, static_cast<uint8_t>(40 * i), 100, 200);
            guids.push_back(setup.Database().GuidFor(file));
        }
        std::ofstream(folder / "quebrada.png", std::ios::binary) << "isto não é um PNG";
        guids.push_back(setup.Database().GuidFor(folder / "quebrada.png"));
        // Material com a textura 5 (pelo GUID, como o editor grava).
        setup.SetTextureFactory([](const ImageData&) { return FakeTexture(); });
        Material material;
        material.albedo = setup.GetTextureByGuid(guids[5]);
        std::string error;
        CHECK(material.albedo != nullptr);
        CHECK(setup.WriteMaterialFile(root / "Assets" / "Pedra.mmat", material, &error));
        guids.push_back(setup.Database().GuidFor(root / "Assets" / "Pedra.mmat"));
    }
    const std::string& materialGuid = guids[7];

    SECTION("PreloadTextures: texturas, as do material e o que não é textura");
    {
        AssetLibrary assets;
        assets.SetRoot(root);
        int created = 0;
        bool sameThread = true;
        const std::thread::id caller = std::this_thread::get_id();
        assets.SetTextureFactory([&](const ImageData& image) {
            ++created;
            sameThread = sameThread && std::this_thread::get_id() == caller;  // a GPU só na thread que pediu
            CHECK(image.width == 32 && image.height == 32);
            return FakeTexture();
        });
        const std::vector<std::string> request = {guids[0], guids[1], guids[2], guids[3], guids[4], guids[0],
                                                  materialGuid, guids[6], std::string(32, '0')};
        CHECK(assets.PreloadTextures(request) == 6);  // 0 a 4, a 5 pelo material; a quebrada e a que não existe não
        CHECK(created == 6);
        CHECK(sameThread);
        CHECK(assets.GetTextureByGuid(guids[3]) != nullptr);
        CHECK(assets.GetTextureByGuid(guids[5]) != nullptr);
        CHECK(created == 6);  // já estavam carregadas
        CHECK(assets.PreloadTextures(request) == 0);
        std::string error;
        CHECK(assets.GetTextureByGuid(guids[6], &error) == nullptr && !error.empty());  // o erro de sempre
    }

    SECTION("Cena: as texturas dos materiais chegam carregadas antes das entidades");
    {
        AssetLibrary assets;
        assets.SetRoot(root);
        assets.AddMesh("Cubo", Mesh::CreateCube());
        int created = 0;
        assets.SetTextureFactory([&](const ImageData&) {
            ++created;
            return FakeTexture();
        });
        std::string error;
        Scene scene;
        Entity& e = scene.CreateEntity("Pedra");
        MeshRenderer& mr = e.Add<MeshRenderer>();
        mr.mesh = assets.GetMesh("Cubo");
        mr.sharedMaterial = assets.GetMaterialByGuid(materialGuid, &error);
        CHECK(mr.sharedMaterial != nullptr);
        const fs::path sceneFile = root / "Assets" / "Pedra.mscene";
        CHECK(SceneSerializer::Save(sceneFile, scene, assets, &error));

        AssetLibrary fresh;
        fresh.SetRoot(root);
        fresh.AddMesh("Cubo", Mesh::CreateCube());
        fresh.SetTextureFactory([&](const ImageData&) {
            ++created;
            return FakeTexture();
        });
        created = 0;
        Scene loaded;
        CHECK(SceneSerializer::Load(sceneFile, loaded, fresh, &error));
        const Entity* pedra = loaded.FindByName("Pedra");
        CHECK(pedra && pedra->Get<MeshRenderer>() && pedra->Get<MeshRenderer>()->sharedMaterial &&
              pedra->Get<MeshRenderer>()->sharedMaterial->albedo != nullptr);
        CHECK(created == 1);
    }

    return TestResult();
}
