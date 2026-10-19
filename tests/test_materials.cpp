// Materiais como asset (.mmat): salvar e carregar, GUID, edição que vale para todos os objetos, recarga do
// disco e migração de cenas antigas (cor e textura direto no Mesh Renderer). Não cria contexto OpenGL.
#include "TestUtil.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/core/Paths.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneSerializer.h>

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace motor;

namespace {
// Texture2D e Mesh de verdade precisam de OpenGL; aqui os ponteiros só identificam os assets embutidos.
alignas(16) unsigned char g_fakeTexture[64] = {};
alignas(16) unsigned char g_fakeMesh[256] = {};

void AddFakeBuiltIns(AssetLibrary& assets) {
    assets.AddTexture("Xadrez", std::shared_ptr<Texture2D>(reinterpret_cast<Texture2D*>(g_fakeTexture), [](Texture2D*) {}));
    assets.AddMesh("Cubo", std::shared_ptr<Mesh>(reinterpret_cast<Mesh*>(g_fakeMesh), [](Mesh*) {}));
}

std::string ReadAll(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void WriteAll(const fs::path& file, const std::string& text) {
    std::ofstream(file, std::ios::binary) << text;
}
} // namespace

int main(int argc, char** argv) {
    const fs::path root = TestDir(argc, argv, "test_materials_tmp") / PathFromUtf8("Jogo Ação");
    const fs::path materials = root / "Assets" / PathFromUtf8("Materiais Ação");
    fs::create_directories(materials);
    std::string error;

    AssetLibrary assets;
    AddFakeBuiltIns(assets);
    assets.SetRoot(root);
    const std::shared_ptr<Texture2D> checker = assets.FindTexture("Xadrez");

    SECTION("Material: salvar, .meta com GUID e carregar");
    const fs::path redFile = materials / PathFromUtf8("Vermelho.mmat");
    Material red;
    red.color = {1.0f, 0.2f, 0.1f, 1.0f};
    red.albedo = checker;
    red.specular = 0.25f;
    red.metallic = 0.75f;
    red.roughness = 0.2f;
    red.normalMap = checker;
    red.emissive = {0.5f, 0.25f, 0.0f};
    red.emissiveIntensity = 3.0f;
    CHECK(AssetTypeFromPath(redFile) == AssetType::Material);
    CHECK(assets.WriteMaterialFile(redFile, red, &error));
    CHECK(fs::exists(AssetDatabase::MetaPathFor(redFile)));
    const std::string guid = assets.Database().FindGuid(redFile);
    CHECK(AssetDatabase::IsValidGuid(guid));
    const std::string text = ReadAll(redFile);
    CHECK(text.find("\"motor-material\"") != std::string::npos);
    CHECK(text.find("\"texture\": \"Xadrez\"") != std::string::npos);

    const std::string redKey = assets.KeyForFile(redFile);
    const std::shared_ptr<Material> loaded = assets.GetMaterial(redKey, &error);
    CHECK(loaded != nullptr);
    CHECK(loaded && loaded->color == red.color && loaded->albedo == checker);
    CHECK(loaded && loaded->specular == 0.25f && loaded->metallic == 0.75f && loaded->roughness == 0.2f);
    CHECK(loaded && loaded->normalMap == checker && !loaded->occlusionMap && !loaded->metallicRoughnessMap);
    CHECK(loaded && loaded->emissive == glm::vec3(0.5f, 0.25f, 0.0f) && loaded->emissiveIntensity == 3.0f);
    CHECK(assets.GetMaterialByGuid(guid) == loaded);  // mesmo objeto: cache por GUID
    CHECK(assets.RefOf(loaded.get()) && assets.RefOf(loaded.get())->guid == guid);

    SECTION("Material: arquivo inválido e campos ausentes");
    Material tolerant;
    CHECK(assets.MaterialFromJson(R"({"format": "motor-material", "version": 1, "specular": 1.5})", tolerant, &error));
    CHECK(tolerant.specular == 1.5f && tolerant.color == glm::vec4(1.0f));
    CHECK(std::abs(tolerant.roughness - RoughnessFromShininess(32.0f)) < 1e-5f);  // versão 1 sem brilho: o padrão de antes
    CHECK(!assets.MaterialFromJson(R"({"format": "motor-scene", "version": 1})", tolerant, &error));
    CHECK(!assets.MaterialFromJson(R"({"format": "motor-material", "version": 99})", tolerant, &error));

    SECTION("Material: modo de alfa, limite do recorte e duas faces (ida e volta; versão 2 fica opaca)");
    {
        Material glass;
        glass.blend = BlendMode::Transparent;
        glass.color.a = 0.3f;
        Material leaf;
        leaf.blend = BlendMode::Cutout;
        leaf.alphaCutoff = 0.35f;
        leaf.doubleSided = true;
        for (const Material* m : {&glass, &leaf}) {
            Material back;
            CHECK(assets.MaterialFromJson(assets.MaterialToJson(*m), back, &error));
            CHECK(back.blend == m->blend && back.alphaCutoff == m->alphaCutoff && back.doubleSided == m->doubleSided);
            CHECK(back.color.a == m->color.a);
        }
        Material old;
        CHECK(assets.MaterialFromJson(R"({"format": "motor-material", "version": 2, "color": [1, 1, 1, 0.5]})", old, &error));
        CHECK(old.blend == BlendMode::Opaque && old.alphaCutoff == 0.5f && !old.doubleSided);
        Material weird;
        CHECK(assets.MaterialFromJson(R"({"format": "motor-material", "version": 3, "blend": "Vidro", "doubleSided": 1})",
                                      weird, &error));
        CHECK(weird.blend == BlendMode::Opaque && !weird.doubleSided);  // valores estranhos: o padrão

        // Material próprio do Mesh Renderer na cena.
        Scene scene;
        Entity& e = scene.CreateEntity("Vidro");
        MeshRenderer& mr = e.Add<MeshRenderer>();
        mr.material.blend = BlendMode::Transparent;
        mr.material.doubleSided = true;
        Scene loaded;
        CHECK(SceneSerializer::FromJson(SceneSerializer::ToJson(scene, assets), loaded, assets, &error));
        const MeshRenderer* back = loaded.FindByName("Vidro") ? loaded.FindByName("Vidro")->Get<MeshRenderer>() : nullptr;
        CHECK(back && back->material.blend == BlendMode::Transparent && back->material.doubleSided);
        CHECK(SceneSerializer::ToJson(scene, assets).find("\"blend\": \"Transparent\"") != std::string::npos);
    }

    SECTION("Material compartilhado: editar muda todos os objetos");
    Scene scene;
    for (const char* name : {"A", "B", "C"}) {
        MeshRenderer& mr = scene.CreateEntity(name).Add<MeshRenderer>();
        mr.mesh = assets.FindMesh("Cubo");
        mr.sharedMaterial = loaded;
        mr.material.color = {0.0f, 0.0f, 0.0f, 1.0f};  // material próprio: ignorado com asset
    }
    scene.FindByName("C")->Get<MeshRenderer>()->sharedMaterial = nullptr;  // C usa o próprio
    loaded->color = {0.1f, 0.9f, 0.2f, 1.0f};
    CHECK(scene.FindByName("A")->Get<MeshRenderer>()->Resolved().color == glm::vec4(0.1f, 0.9f, 0.2f, 1.0f));
    CHECK(scene.FindByName("B")->Get<MeshRenderer>()->Resolved().color == glm::vec4(0.1f, 0.9f, 0.2f, 1.0f));
    CHECK(scene.FindByName("C")->Get<MeshRenderer>()->Resolved().color == glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    CHECK(assets.SaveMaterial(loaded.get(), &error));
    Material unknown;
    CHECK(!assets.SaveMaterial(&unknown, &error));  // não veio de arquivo

    SECTION("Cena: material por GUID, ida e volta");
    const std::string sceneJson = SceneSerializer::ToJson(scene, assets);
    CHECK(sceneJson.find(guid) != std::string::npos);
    {
        AssetLibrary fresh;
        AddFakeBuiltIns(fresh);
        fresh.SetRoot(root);
        Scene reloaded;
        CHECK(SceneSerializer::FromJson(sceneJson, reloaded, fresh, &error));
        const MeshRenderer* a = reloaded.FindByName("A")->Get<MeshRenderer>();
        const MeshRenderer* b = reloaded.FindByName("B")->Get<MeshRenderer>();
        CHECK(a->sharedMaterial && a->sharedMaterial == b->sharedMaterial);
        CHECK(a->sharedMaterial && a->sharedMaterial->color == glm::vec4(0.1f, 0.9f, 0.2f, 1.0f));  // o que foi salvo
        CHECK(a->sharedMaterial && a->sharedMaterial->albedo == fresh.FindTexture("Xadrez"));
        CHECK(!reloaded.FindByName("C")->Get<MeshRenderer>()->sharedMaterial);
        CHECK(SceneSerializer::ToJson(reloaded, fresh) == sceneJson);
    }

    SECTION("Material renomeado e movido (com o .meta) continua achado pelo GUID");
    const fs::path movedDir = root / "Assets" / "Outros";
    fs::create_directories(movedDir);
    const fs::path movedFile = movedDir / PathFromUtf8("Vermelho escuro.mmat");
    fs::rename(redFile, movedFile);
    fs::rename(AssetDatabase::MetaPathFor(redFile), AssetDatabase::MetaPathFor(movedFile));
    {
        AssetLibrary fresh;
        AddFakeBuiltIns(fresh);
        fresh.SetRoot(root);
        Scene reloaded;
        CHECK(SceneSerializer::FromJson(sceneJson, reloaded, fresh, &error));
        const auto& material = reloaded.FindByName("A")->Get<MeshRenderer>()->sharedMaterial;
        CHECK(material != nullptr);
        CHECK(material && fresh.RefOf(material.get())->name == fresh.KeyForFile(movedFile));
    }

    SECTION("Material editado fora do editor é relido no mesmo objeto");
    {
        AssetLibrary fresh;
        AddFakeBuiltIns(fresh);
        fresh.SetRoot(root);
        const auto material = fresh.GetMaterialByGuid(guid);
        CHECK(material != nullptr);
        CHECK(fresh.ReloadChangedMaterials() == 0);
        Material changed = *material;
        changed.color = {0.0f, 0.0f, 1.0f, 1.0f};
        WriteAll(movedFile, fresh.MaterialToJson(changed));
        fs::last_write_time(movedFile, fs::last_write_time(movedFile) + std::chrono::seconds(2));
        CHECK(fresh.ReloadChangedMaterials() == 1);
        CHECK(material->color == glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
        CHECK(fresh.GetMaterialByGuid(guid) == material);
    }

    SECTION("Migração: cena versão 4 com cor e textura no Mesh Renderer");
    const char* v4 = R"({
      "format": "motor-scene", "version": 4,
      "environment": {"skyColor": [0.1, 0.1, 0.1], "fogDensity": 0.0, "ambient": [0.2, 0.2, 0.2]},
      "entities": [
        {"id": 1, "name": "Caixa", "active": true, "parent": 0,
         "transform": {"position": [0, 1, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]},
         "components": [{"type": "MeshRenderer", "mesh": "Cubo", "texture": "Xadrez",
                         "color": [0.8, 0.3, 0.2, 1.0], "specular": 0.7, "shininess": 16.0}]}
      ]})";
    Scene old;
    CHECK(SceneSerializer::FromJson(v4, old, assets, &error));
    const MeshRenderer* oldRenderer = old.FindByName("Caixa") ? old.FindByName("Caixa")->Get<MeshRenderer>() : nullptr;
    CHECK(oldRenderer != nullptr);
    if (oldRenderer) {
        CHECK(!oldRenderer->sharedMaterial);  // sem asset: material próprio, nada perdido
        CHECK(oldRenderer->mesh == assets.FindMesh("Cubo"));
        CHECK(oldRenderer->material.albedo == checker);
        CHECK(oldRenderer->material.color == glm::vec4(0.8f, 0.3f, 0.2f, 1.0f));
        CHECK(oldRenderer->material.specular == 0.7f);
        CHECK(std::abs(oldRenderer->material.roughness - RoughnessFromShininess(16.0f)) < 1e-5f);
        CHECK(&oldRenderer->Resolved() == &oldRenderer->material);
    }
    // Salva na versão nova e lê de volta igual.
    const std::string upgraded = SceneSerializer::ToJson(old, assets);
    CHECK(upgraded.find("\"version\": 6") != std::string::npos);
    CHECK(upgraded.find("\"material\": null") != std::string::npos);
    Scene again;
    CHECK(SceneSerializer::FromJson(upgraded, again, assets, &error));
    CHECK(again == old);

    SECTION("Migração: versão 1 (\"meshRenderer\" fixo)");
    Scene v1;
    CHECK(SceneSerializer::FromJson(R"({"format": "motor-scene", "version": 1, "entities": [
        {"id": 1, "name": "Velha", "parent": 0, "meshRenderer": {"mesh": "Cubo", "color": [0, 1, 0, 1]}}]})",
                                    v1, assets, &error));
    const MeshRenderer* v1Renderer = v1.FindByName("Velha") ? v1.FindByName("Velha")->Get<MeshRenderer>() : nullptr;
    CHECK(v1Renderer && !v1Renderer->sharedMaterial && v1Renderer->material.color == glm::vec4(0, 1, 0, 1));

    SECTION("Material que sumiu: o componente fica sem asset (e usa o próprio)");
    Scene missing;
    CHECK(SceneSerializer::FromJson(R"({"format": "motor-scene", "version": 5, "entities": [
        {"id": 1, "name": "X", "parent": 0, "components": [{"type": "MeshRenderer", "mesh": "Cubo",
         "material": {"guid": "0123456789abcdef0123456789abcdef", "path": "Assets/Nada.mmat"}}]}]})",
                                    missing, assets, &error));
    CHECK(missing.FindByName("X") && !missing.FindByName("X")->Get<MeshRenderer>()->sharedMaterial);

    return TestResult();
}
