#include "motor/scene/ModelImport.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/assets/Model.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/renderer/Mesh.h"
#include "motor/scene/Components.h"
#include "motor/scene/Prefab.h"
#include "motor/scene/Scene.h"

#include <format>
#include <fstream>
#include <map>

namespace motor {

namespace fs = std::filesystem;

namespace {
void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

// Nome de arquivo sem caracteres que o Windows recusa.
std::string SafeFileName(std::string name) {
    for (char& c : name) {
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' ||
            static_cast<unsigned char>(c) < 32) {
            c = '_';
        }
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    return name.empty() ? std::string("sem_nome") : name;
}

bool WriteBytes(const fs::path& file, const std::vector<uint8_t>& bytes) {
    fs::path temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
            return false;
        }
    }
    std::error_code ec;
    fs::rename(temp, file, ec);
    if (ec) {
        fs::remove(file, ec);
        fs::rename(temp, file, ec);
    }
    return !ec;
}

std::string ImageExtension(const ModelImage& image) {
    if (image.mimeType == "image/jpeg") return ".jpg";
    if (image.bytes.size() >= 3 && image.bytes[0] == 0xFF && image.bytes[1] == 0xD8 && image.bytes[2] == 0xFF) return ".jpg";
    return ".png";
}

// Textura usada por um material: arquivo do projeto (externa dentro de Assets/ ou gravada na pasta do modelo).
std::optional<AssetRef> ImportImage(const ModelImage& image, const fs::path& folder, AssetLibrary& assets,
                                    ModelImportResult& result) {
    fs::path file;
    if (!image.bytes.empty()) {
        file = folder / PathFromUtf8(SafeFileName(image.name) + ImageExtension(image));
        if (!WriteBytes(file, image.bytes)) {
            log::Warn("Importar modelo: não foi possível gravar a textura '{}'", ToUtf8(file));
            return std::nullopt;
        }
        ++result.textures;
    } else if (!image.file.empty()) {
        file = image.file;
        if (assets.KeyForFile(file).empty() || !IsInside(file, assets.Database().AssetsDir())) {
            // Fora do projeto: copia para a pasta do modelo.
            const fs::path copy = folder / file.filename();
            std::error_code ec;
            fs::copy_file(file, copy, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                log::Warn("Importar modelo: textura '{}' não encontrada", ToUtf8(file));
                return std::nullopt;
            }
            file = copy;
            ++result.textures;
        }
    } else {
        return std::nullopt;
    }
    const std::string guid = assets.Database().GuidFor(file);
    const std::string key = assets.KeyForFile(file);
    if (guid.empty() || key.empty()) return std::nullopt;
    return AssetRef{key, guid};
}

// Texturas e materiais do modelo: um .mmat por material (e as imagens) na pasta de materiais do modelo.
// `materials` fica com os materiais carregados, na ordem do modelo (nulo se não carregou).
bool ImportMaterials(const ModelData& data, AssetLibrary& assets, ModelImportResult& result,
                     std::vector<std::shared_ptr<Material>>& materials, std::string* error) {
    std::error_code ec;
    if (!data.materials.empty()) fs::create_directories(result.folder, ec);
    std::map<int, std::optional<AssetRef>> imageRefs;
    materials.assign(data.materials.size(), nullptr);
    for (size_t i = 0; i < data.materials.size(); ++i) {
        const ModelMaterial& m = data.materials[i];
        auto image = [&](int index) -> std::optional<AssetRef> {
            if (index < 0) return std::nullopt;
            auto it = imageRefs.find(index);
            if (it == imageRefs.end()) {
                it = imageRefs.emplace(index, ImportImage(data.images[static_cast<size_t>(index)], result.folder, assets, result)).first;
            }
            return it->second;
        };
        const MaterialTextureRefs textures{image(m.image), image(m.normalImage), image(m.metallicRoughnessImage),
                                           image(m.occlusionImage), image(m.emissiveImage)};
        Material material;
        material.color = m.color;
        material.metallic = m.metallic;
        material.roughness = m.roughness;
        material.normalStrength = m.normalScale;
        material.occlusionStrength = m.occlusionStrength;
        material.emissive = m.emissive;
        material.emissiveIntensity = m.emissiveIntensity;
        const fs::path file = result.folder / PathFromUtf8(SafeFileName(m.name) + kMaterialFileExtension);
        std::string problem;
        if (!assets.WriteMaterialFile(file, material, textures, &problem)) {
            SetError(error, std::move(problem));
            return false;
        }
        ++result.materials;
        // Já carregado (reimportação): relê agora, no mesmo objeto, para as instâncias verem o material novo.
        assets.ReloadChangedMaterials();
        materials[i] = assets.GetMaterial(assets.KeyForFile(file), &problem);
        if (!materials[i]) log::Warn("Importar modelo: material '{}' não carregado: {}", m.name, problem);
    }
    return true;
}

// Entidades do prefab: a raiz (com o Animator, se houver animação ou pele) e um filho por nó que tem malha
// ou leva a um nó com malha; nó com várias primitivas ganha um filho por primitiva.
void BuildPrefabContent(const std::shared_ptr<Model>& model, const std::vector<std::shared_ptr<Material>>& materials,
                        const std::string& stem, Scene& content) {
    const ModelData& data = model->data;
    uint32_t nextSource = 1;
    auto link = [&](Entity& e) { e.Add<PrefabInstance>().link.source = nextSource++; };
    auto addRenderer = [&](Entity& e, size_t primitive) {
        MeshRenderer& mr = e.Add<MeshRenderer>();
        mr.mesh = model->meshes[primitive].mesh;
        const int material = data.primitives[primitive].material;
        if (material >= 0 && static_cast<size_t>(material) < materials.size()) mr.sharedMaterial = materials[static_cast<size_t>(material)];
    };

    Entity& root = content.CreateEntity(stem);
    const EntityId rootId = root.id;
    bool skinned = false;
    for (const auto& m : model->meshes) skinned = skinned || m.mesh->IsSkinned();
    if (!model->clips.empty() || skinned) {
        Animator& animator = root.Add<Animator>();
        animator.model = model;
        if (!model->clips.empty()) animator.clip = model->clips.front()->name;
    }
    link(*content.Find(rootId));

    // Nós que viram entidade: não são ossos e têm malha (sem pele) ou um descendente que vira entidade.
    const size_t n = data.nodes.size();
    std::vector<bool> keep(n, false);
    for (size_t i = n; i-- > 0;) {
        const ModelNode& node = data.nodes[i];
        if (data.IsJoint(static_cast<int>(i))) continue;
        if (node.skin < 0 && !node.primitives.empty()) keep[i] = true;
        if (keep[i] && node.parent >= 0 && !data.IsJoint(node.parent)) keep[static_cast<size_t>(node.parent)] = true;
    }
    std::vector<EntityId> entityOf(n, kNullEntity);
    for (size_t i = 0; i < n; ++i) {
        const ModelNode& node = data.nodes[i];
        if (node.skin >= 0) {
            // Malha com pele: a posição vem das juntas, então fica na origem do modelo (como manda o glTF).
            for (size_t k = 0; k < node.primitives.size(); ++k) {
                const auto p = static_cast<size_t>(node.primitives[k]);
                Entity& e = content.CreateEntity(node.primitives.size() == 1 ? node.name : data.primitives[p].name, rootId);
                addRenderer(e, p);
                link(e);
            }
            continue;
        }
        if (!keep[i]) continue;
        const EntityId parent = node.parent >= 0 && entityOf[static_cast<size_t>(node.parent)] != kNullEntity
                                    ? entityOf[static_cast<size_t>(node.parent)]
                                    : rootId;
        Entity& e = content.CreateEntity(node.name, parent);
        e.transform.position = node.local.translation;
        e.transform.rotation = node.local.rotation;
        e.transform.scale = node.local.scale;
        entityOf[i] = e.id;
        if (node.primitives.size() == 1) {
            addRenderer(e, static_cast<size_t>(node.primitives[0]));
            link(e);
        } else {
            const EntityId id = e.id;
            link(e);
            for (int p : node.primitives) {
                Entity& part = content.CreateEntity(data.primitives[static_cast<size_t>(p)].name, id);
                addRenderer(part, static_cast<size_t>(p));
                link(part);
            }
        }
    }
}
} // namespace

fs::path ModelPrefabPath(const fs::path& modelFile) {
    fs::path prefab = modelFile;
    prefab.replace_extension(kPrefabFileExtension);
    return prefab;
}

bool ModelNeedsImport(const fs::path& modelFile) {
    std::error_code ec;
    const auto prefabTime = fs::last_write_time(ModelPrefabPath(modelFile), ec);
    if (ec) return true;
    const auto modelTime = fs::last_write_time(modelFile, ec);
    return !ec && prefabTime < modelTime;
}

bool ImportModel(const fs::path& modelFile, AssetLibrary& assets, ModelImportResult* resultOut, std::string* error) {
    const std::string key = assets.KeyForFile(modelFile);
    if (key.empty()) {
        SetError(error, std::format("'{}' está fora do projeto.", ToUtf8(modelFile.filename())));
        return false;
    }
    std::shared_ptr<Model> model = assets.GetModel(key, error);
    if (!model) return false;
    const ModelData& data = model->data;

    ModelImportResult result;
    const std::string stem = ToUtf8(modelFile.stem());
    result.prefab = ModelPrefabPath(modelFile);
    result.folder = modelFile.parent_path() / PathFromUtf8(stem + "_materiais");

    std::vector<std::shared_ptr<Material>> materials;
    if (!ImportMaterials(data, assets, result, materials, error)) return false;

    Scene content;
    BuildPrefabContent(model, materials, stem, content);
    result.entities = content.Entities().size();

    if (!PrefabLibrary::WriteFile(result.prefab, content, assets, error)) return false;
    log::Info("Modelo importado: {} -> {} ({} entidades, {} materiais, {} texturas)", key,
              assets.KeyForFile(result.prefab), result.entities, result.materials, result.textures);
    if (resultOut) *resultOut = std::move(result);
    return true;
}

size_t ImportModelsInProject(AssetLibrary& assets) {
    size_t count = 0;
    for (const std::string& key : assets.ScanFiles(AssetType::Model)) {
        const fs::path file = assets.FileForKey(key);
        if (!ModelNeedsImport(file)) continue;
        std::string error;
        if (ImportModel(file, assets, nullptr, &error)) {
            ++count;
        } else {
            log::Warn("Modelo '{}' não importado: {}", key, error);
        }
    }
    return count;
}

} // namespace motor
