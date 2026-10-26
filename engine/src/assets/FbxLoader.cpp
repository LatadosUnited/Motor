// Leitura de FBX com a ufbx. Converte para o mesmo ModelData do glTF (GltfLoader.cpp): nós em ordem (pai antes
// do filho), uma malha por material de cada malha do arquivo, materiais, imagens, peles e clipes. Daí em diante
// (BuildModel, prefab do modelo, Animator) é tudo igual ao glTF.
//
// A ufbx já entrega a cena no espaço do Motor: Y para cima, metros (o Mixamo grava em centímetros), sem os
// pivôs e as "transformações de geometria" do FBX, e as animações amostradas em quadros (ufbx_bake_anim).
#include "motor/assets/Model.h"

#include "motor/common/FileRead.h"
#include "motor/common/Text.h"
#include "motor/core/Paths.h"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <memory>
#include <set>
#include <unordered_map>

#include <ufbx.h>

namespace motor {

namespace fs = std::filesystem;

namespace {

struct SceneDeleter {
    void operator()(ufbx_scene* s) const { ufbx_free_scene(s); }
};
struct BakeDeleter {
    void operator()(ufbx_baked_anim* b) const { ufbx_free_baked_anim(b); }
};

std::string Str(const ufbx_string& s) {
    return std::string(s.data, s.length);
}

// Nomes únicos: vazio vira o `fallback`, repetido ganha " (2)", " (3)"...
class UniqueNames {
public:
    std::string Make(std::string base, const std::string& fallback) {
        if (base.empty()) base = fallback;
        std::string result = base;
        for (int n = 2; m_used.contains(result); ++n) result = std::format("{} ({})", base, n);
        m_used.insert(result);
        return result;
    }

private:
    std::set<std::string> m_used;
};

// Nome do FBX sem o prefixo de tipo que alguns exportadores põem ("Model::Corpo", "Material::Pele").
std::string CleanName(const ufbx_string& s) {
    std::string name = Str(s);
    if (const size_t colons = name.rfind("::"); colons != std::string::npos) name.erase(0, colons + 2);
    return name;
}

glm::mat4 ToMat4(const ufbx_matrix& m) {
    glm::mat4 out(1.0f);
    for (int c = 0; c < 4; ++c) {
        out[c] = glm::vec4(static_cast<float>(m.cols[c].x), static_cast<float>(m.cols[c].y),
                           static_cast<float>(m.cols[c].z), c == 3 ? 1.0f : 0.0f);
    }
    return out;
}

glm::vec3 ToVec3(const ufbx_vec3& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

glm::quat ToQuat(const ufbx_quat& q) {
    return glm::normalize(glm::quat(static_cast<float>(q.w), static_cast<float>(q.x), static_cast<float>(q.y),
                                    static_cast<float>(q.z)));
}

std::string MimeFromName(const std::string& name) {
    std::string ext = ToUtf8(PathFromUtf8(name).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".tga") return "image/tga";
    return {};
}

// Arquivo de uma textura externa: o caminho relativo ao .fbx, o absoluto gravado e, por fim, só o nome do
// arquivo ao lado do .fbx (o caso comum de quem copia o modelo com as texturas para outra pasta).
fs::path FindTextureFile(const ufbx_texture& t, const fs::path& baseDir) {
    std::error_code ec;
    std::vector<fs::path> candidates;
    if (t.relative_filename.length > 0) candidates.push_back(baseDir / PathFromUtf8(Str(t.relative_filename)));
    if (t.absolute_filename.length > 0) candidates.push_back(PathFromUtf8(Str(t.absolute_filename)));
    if (t.filename.length > 0) candidates.push_back(PathFromUtf8(Str(t.filename)));
    for (const fs::path& c : std::vector<fs::path>(candidates)) {
        if (c.has_filename()) {
            candidates.push_back(baseDir / c.filename());
            candidates.push_back(baseDir / "textures" / c.filename());
        }
    }
    for (const fs::path& c : candidates) {
        if (!c.empty() && fs::is_regular_file(c, ec)) return c.lexically_normal();
    }
    return {};
}

class Converter {
public:
    Converter(const ufbx_scene& scene, const fs::path& baseDir, std::string fileStem)
        : m_scene(scene), m_baseDir(baseDir), m_fileStem(std::move(fileStem)) {}

    bool Run(ModelData& model, std::string* error) {
        ReadMaterials(model);
        ReadNodes(model);
        ReadMeshes(model);
        if (!ReadClips(model, error)) return false;
        if (model.primitives.empty() && model.nodes.empty()) {
            SetError(error, "FBX sem malhas nem nós.");
            return false;
        }
        return true;
    }

private:
    // Imagem de uma textura (embutida ou arquivo ao lado), uma vez por arquivo.
    int ImageOf(const ufbx_texture* texture, ModelData& model) {
        if (!texture) return -1;
        if (const auto it = m_images.find(texture); it != m_images.end()) return it->second;
        ModelImage image;
        const std::string fileName = Str(texture->relative_filename.length ? texture->relative_filename : texture->filename);
        if (texture->content.size > 0) {
            const auto* data = static_cast<const uint8_t*>(texture->content.data);
            image.bytes.assign(data, data + texture->content.size);
            image.mimeType = MimeFromName(fileName);
        } else {
            image.file = FindTextureFile(*texture, m_baseDir);
            if (image.file.empty()) {
                m_images[texture] = -1;  // arquivo não achado: o material fica sem a textura
                return -1;
            }
            image.mimeType = MimeFromName(ToUtf8(image.file));
        }
        std::string name = fileName.empty() ? CleanName(texture->name) : ToUtf8(PathFromUtf8(fileName).stem());
        image.name = m_imageNames.Make(std::move(name), std::format("imagem{}", model.images.size()));
        const int index = static_cast<int>(model.images.size());
        model.images.push_back(std::move(image));
        m_images[texture] = index;
        return index;
    }

    // Material PBR a partir do que a ufbx deduz do FBX (Lambert/Phong do Maya e do Max, Stingray, 3ds Max
    // Physical...). As cores do FBX já estão em sRGB, como no Motor.
    void ReadMaterials(ModelData& model) {
        UniqueNames names;
        for (size_t i = 0; i < m_scene.materials.count; ++i) {
            const ufbx_material& m = *m_scene.materials.data[i];
            const ufbx_material_pbr_maps& pbr = m.pbr;
            ModelMaterial out;
            out.name = names.Make(CleanName(m.name), std::format("material{}", i));
            const float factor = pbr.base_factor.has_value ? static_cast<float>(pbr.base_factor.value_real) : 1.0f;
            if (pbr.base_color.has_value) {
                out.color = glm::vec4(ToVec3(pbr.base_color.value_vec3) * factor, 1.0f);
                if (pbr.base_color.value_components >= 4) out.color.a = static_cast<float>(pbr.base_color.value_vec4.w);
            }
            if (pbr.base_color.texture_enabled) out.image = ImageOf(pbr.base_color.texture, model);
            out.metallic = pbr.metalness.has_value ? std::clamp(static_cast<float>(pbr.metalness.value_real), 0.0f, 1.0f)
                                                   : 0.0f;
            out.roughness = pbr.roughness.has_value
                                ? std::clamp(static_cast<float>(pbr.roughness.value_real), 0.0f, 1.0f)
                                : 0.6f;
            if (pbr.normal_map.texture_enabled) out.normalImage = ImageOf(pbr.normal_map.texture, model);
            if (pbr.emission_color.has_value) {
                const float strength = pbr.emission_factor.has_value ? static_cast<float>(pbr.emission_factor.value_real) : 1.0f;
                out.emissive = glm::max(ToVec3(pbr.emission_color.value_vec3), glm::vec3(0.0f));
                out.emissiveIntensity = std::max(strength, 0.0f);
            }
            if (pbr.emission_color.texture_enabled) out.emissiveImage = ImageOf(pbr.emission_color.texture, model);
            // Opacidade: menor que 1 vira transparente (vidro, cabelo em cartões).
            if (pbr.opacity.has_value && pbr.opacity.value_real < 0.999) {
                out.color.a *= static_cast<float>(std::clamp(pbr.opacity.value_real, 0.0, 1.0));
            }
            if (out.color.a < 0.999f || (pbr.opacity.texture_enabled && pbr.opacity.texture)) {
                out.blend = BlendMode::Transparent;
            }
            model.materials.push_back(std::move(out));
            m_materialIndex[&m] = static_cast<int>(model.materials.size()) - 1;
        }
    }

    // Nós: a raiz do FBX não entra (os filhos dela são as raízes do modelo).
    void ReadNodes(ModelData& model) {
        UniqueNames names;
        auto add = [&](auto& self, const ufbx_node* node, int parent) -> void {
            ModelNode n;
            n.name = names.Make(CleanName(node->name), std::format("no{}", node->typed_id));
            n.parent = parent;
            n.local.translation = ToVec3(node->local_transform.translation);
            n.local.rotation = ToQuat(node->local_transform.rotation);
            n.local.scale = ToVec3(node->local_transform.scale);
            const int index = static_cast<int>(model.nodes.size());
            m_nodeIndex[node] = index;
            model.nodes.push_back(std::move(n));
            for (size_t i = 0; i < node->children.count; ++i) self(self, node->children.data[i], index);
        };
        const ufbx_node* root = m_scene.root_node;
        for (size_t i = 0; root && i < root->children.count; ++i) add(add, root->children.data[i], -1);
    }

    int NodeIndex(const ufbx_node* node) const {
        const auto it = m_nodeIndex.find(node);
        return it == m_nodeIndex.end() ? -1 : it->second;
    }

    // Vértice completo para juntar os repetidos (ufbx_generate_indices compara os bytes).
    struct FullVertex {
        Vertex vertex;
        SkinWeights skin;
    };
    // Sem bytes de enchimento: lixo neles faria dois vértices iguais parecerem diferentes.
    static_assert(sizeof(FullVertex) == sizeof(float) * 16);

    // Uma malha do FBX vira uma malha por material (como as primitivas do glTF). Polígonos viram triângulos.
    //
    // Malha com pele: os vértices vão para o espaço do modelo na pose de "bind" e o nó dela fica neutro na raiz,
    // como no glTF (o Motor aplica a matriz da entidade da malha por cima da pele). Os exportadores costumam pôr a
    // malha num nó girado e escalado (o Blender: -90° em X e escala 100); sem isso a malha sairia aplicada duas vezes.
    void ReadMeshes(ModelData& model) {
        UniqueNames names;
        std::unordered_map<const ufbx_mesh*, std::vector<int>> primitivesOfMesh;
        std::unordered_map<const ufbx_skin_deformer*, int> skinIndex;
        for (size_t mi = 0; mi < m_scene.meshes.count; ++mi) {
            const ufbx_mesh& mesh = *m_scene.meshes.data[mi];
            const ufbx_skin_deformer* skin = mesh.skin_deformers.count > 0 ? mesh.skin_deformers.data[0] : nullptr;
            if (skin && !skinIndex.contains(skin)) {
                ModelSkin s;
                for (size_t c = 0; c < skin->clusters.count; ++c) {
                    const ufbx_skin_cluster& cluster = *skin->clusters.data[c];
                    s.joints.push_back(NodeIndex(cluster.bone_node));
                    s.inverseBind.push_back(glm::inverse(ToMat4(cluster.bind_to_world)));
                }
                skinIndex[skin] = static_cast<int>(model.skins.size());
                model.skins.push_back(std::move(s));
            }

            const std::string base = names.Make(CleanName(mesh.name), std::format("malha{}", mi));
            // Geometria -> modelo na pose de bind (a mesma em todos os ossos da pele).
            glm::mat4 bind(1.0f);
            if (skin && skin->clusters.count > 0) {
                const ufbx_skin_cluster& first = *skin->clusters.data[0];
                bind = ToMat4(first.bind_to_world) * ToMat4(first.geometry_to_bone);
            }
            const glm::mat3 bindNormal = glm::transpose(glm::inverse(glm::mat3(bind)));
            std::vector<uint32_t> triangle(mesh.max_face_triangles * 3);
            const size_t parts = std::max<size_t>(mesh.material_parts.count, 1);
            for (size_t pi = 0; pi < parts; ++pi) {
                std::vector<FullVertex> corners;
                auto addFace = [&](ufbx_face face) {
                    const uint32_t count = ufbx_triangulate_face(triangle.data(), triangle.size(), &mesh, face);
                    for (uint32_t k = 0; k < count * 3; ++k) {
                        FullVertex v = Corner(mesh, skin, triangle[k]);
                        if (skin) {
                            v.vertex.position = glm::vec3(bind * glm::vec4(v.vertex.position, 1.0f));
                            const glm::vec3 n = bindNormal * v.vertex.normal;
                            if (glm::length(n) > 1e-12f) v.vertex.normal = glm::normalize(n);
                        }
                        corners.push_back(v);
                    }
                };
                if (mesh.material_parts.count > 0) {
                    const ufbx_mesh_part& part = mesh.material_parts.data[pi];
                    for (size_t f = 0; f < part.face_indices.count; ++f) addFace(mesh.faces.data[part.face_indices.data[f]]);
                } else {
                    for (size_t f = 0; f < mesh.faces.count; ++f) addFace(mesh.faces.data[f]);
                }
                if (corners.empty()) continue;

                ModelPrimitive prim;
                std::vector<uint32_t> indices(corners.size());
                ufbx_vertex_stream stream{corners.data(), corners.size(), sizeof(FullVertex)};
                ufbx_error error{};
                const size_t unique = ufbx_generate_indices(&stream, 1, indices.data(), indices.size(), nullptr, &error);
                if (error.type != UFBX_ERROR_NONE) continue;
                corners.resize(unique);
                prim.mesh.indices = std::move(indices);
                prim.mesh.vertices.reserve(unique);
                for (const FullVertex& v : corners) prim.mesh.vertices.push_back(v.vertex);
                if (skin) {
                    prim.weights.reserve(unique);
                    for (const FullVertex& v : corners) prim.weights.push_back(v.skin);
                }
                const std::string name = parts > 1 ? std::format("{}.{}", base, pi) : base;
                prim.name = m_primitiveNames.Make(name, name);
                if (pi < mesh.materials.count) {
                    if (const auto it = m_materialIndex.find(mesh.materials.data[pi]); it != m_materialIndex.end()) {
                        prim.material = it->second;
                    }
                }
                primitivesOfMesh[&mesh].push_back(static_cast<int>(model.primitives.size()));
                model.primitives.push_back(std::move(prim));
            }
        }
        // Cada nó com malha aponta para as malhas dela (e a pele). O mesmo FBX pode instanciar uma malha em dois nós.
        for (const auto& [node, index] : m_nodeIndex) {
            if (!node->mesh) continue;
            const auto it = primitivesOfMesh.find(node->mesh);
            if (it == primitivesOfMesh.end()) continue;
            ModelNode& n = model.nodes[static_cast<size_t>(index)];
            n.primitives = it->second;
            if (node->mesh->skin_deformers.count > 0) {
                n.skin = skinIndex[node->mesh->skin_deformers.data[0]];
                if (node->children.count == 0) {  // vértices já no espaço do modelo: nó neutro na raiz
                    n.parent = -1;
                    n.local = JointTransform{};
                }
            }
        }
    }

    FullVertex Corner(const ufbx_mesh& mesh, const ufbx_skin_deformer* skin, uint32_t index) const {
        FullVertex v{};
        v.vertex.position = ToVec3(ufbx_get_vertex_vec3(&mesh.vertex_position, index));
        if (mesh.vertex_normal.exists) {
            const glm::vec3 n = ToVec3(ufbx_get_vertex_vec3(&mesh.vertex_normal, index));
            const float len = glm::length(n);
            v.vertex.normal = len > 1e-12f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
        }
        if (mesh.vertex_uv.exists) {
            const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh.vertex_uv, index);
            // O FBX já usa a linha 0 embaixo, como as texturas do Motor (o glTF é que inverte).
            v.vertex.uv = {static_cast<float>(uv.x), static_cast<float>(uv.y)};
        }
        v.skin.weights = {1.0f, 0.0f, 0.0f, 0.0f};
        if (skin) {
            const uint32_t vertex = mesh.vertex_indices.data[index];
            if (vertex < skin->vertices.count) {
                // Os pesos vêm do maior para o menor: os 4 primeiros, normalizados.
                const ufbx_skin_vertex& sv = skin->vertices.data[vertex];
                const uint32_t count = std::min<uint32_t>(sv.num_weights, 4);
                float sum = 0.0f;
                glm::vec4 joints(0.0f), weights(0.0f);
                for (uint32_t k = 0; k < count; ++k) {
                    const ufbx_skin_weight& w = skin->weights.data[sv.weight_begin + k];
                    joints[static_cast<int>(k)] = static_cast<float>(w.cluster_index);
                    weights[static_cast<int>(k)] = static_cast<float>(w.weight);
                    sum += static_cast<float>(w.weight);
                }
                if (sum > 1e-6f) {
                    v.skin.joints = joints;
                    v.skin.weights = weights / sum;
                }
            }
        }
        return v;
    }

    // Nome do clipe: o da pilha de animação, sem o "Armature|" do Blender. Nome genérico ("mixamo.com",
    // "Take 001") no único clipe do arquivo vira o nome do arquivo: "X Bot@Idle.fbx" -> "Idle", como na Unity.
    std::string ClipName(const ufbx_anim_stack& stack, bool onlyClip) const {
        std::string name = CleanName(stack.name);
        if (const size_t bar = name.rfind('|'); bar != std::string::npos) name.erase(0, bar + 1);
        static const char* const kGeneric[] = {"mixamo.com", "Take 001", "Take 01", "Take001", "Unreal Take",
                                               "Default Take"};
        const bool generic = name.empty() || std::any_of(std::begin(kGeneric), std::end(kGeneric),
                                                         [&](const char* g) { return name == g; });
        if (generic && onlyClip && !m_fileStem.empty()) {
            const size_t at = m_fileStem.rfind('@');
            return at == std::string::npos ? m_fileStem : m_fileStem.substr(at + 1);
        }
        return name;
    }

    // Clipes: cada pilha de animação amostrada (translação, rotação e escala por nó), com o tempo começando em 0.
    // Pilhas sem nenhuma curva (o Mixamo grava uma "Take 001" vazia ao lado da animação) não viram clipe.
    bool ReadClips(ModelData& model, std::string* error) {
        std::vector<std::pair<const ufbx_anim_stack*, AnimationClip>> clips;
        for (size_t si = 0; si < m_scene.anim_stacks.count; ++si) {
            const ufbx_anim_stack& stack = *m_scene.anim_stacks.data[si];
            ufbx_bake_opts opts{};
            opts.trim_start_time = true;  // o clipe começa no tempo 0
            ufbx_error bakeError{};
            std::unique_ptr<ufbx_baked_anim, BakeDeleter> bake(ufbx_bake_anim(&m_scene, stack.anim, &opts, &bakeError));
            if (!bake) {
                SetError(error, std::format("FBX: animação '{}' não lida ({}).", Str(stack.name), Str(bakeError.description)));
                return false;
            }
            AnimationClip clip;
            for (size_t bi = 0; bi < bake->nodes.count; ++bi) {
                const ufbx_baked_node& baked = bake->nodes.data[bi];
                if (baked.typed_id >= m_scene.nodes.count) continue;
                const int node = NodeIndex(m_scene.nodes.data[baked.typed_id]);
                if (node < 0) continue;
                auto channel = [&](AnimationPath path) {
                    AnimationChannel c;
                    c.path = path;
                    c.interpolation = AnimationInterpolation::Linear;
                    c.jointIndex = node;
                    c.joint = model.nodes[static_cast<size_t>(node)].name;
                    return c;
                };
                auto addVec3 = [&](AnimationPath path, const ufbx_baked_vec3_list& keys) {
                    if (keys.count == 0) return;
                    AnimationChannel c = channel(path);
                    for (size_t k = 0; k < keys.count; ++k) {
                        c.times.push_back(static_cast<float>(keys.data[k].time));
                        c.values.emplace_back(ToVec3(keys.data[k].value), 0.0f);
                    }
                    clip.duration = std::max(clip.duration, c.times.back());
                    clip.channels.push_back(std::move(c));
                };
                addVec3(AnimationPath::Translation, baked.translation_keys);
                if (baked.rotation_keys.count > 0) {
                    AnimationChannel c = channel(AnimationPath::Rotation);
                    for (size_t k = 0; k < baked.rotation_keys.count; ++k) {
                        const glm::quat q = ToQuat(baked.rotation_keys.data[k].value);
                        c.times.push_back(static_cast<float>(baked.rotation_keys.data[k].time));
                        c.values.emplace_back(q.x, q.y, q.z, q.w);  // como o glTF: x, y, z, w
                    }
                    clip.duration = std::max(clip.duration, c.times.back());
                    clip.channels.push_back(std::move(c));
                }
                addVec3(AnimationPath::Scale, baked.scale_keys);
            }
            if (clip.channels.empty()) continue;
            clip.duration = std::max(clip.duration, static_cast<float>(bake->playback_duration));
            clips.emplace_back(&stack, std::move(clip));
        }
        UniqueNames names;
        for (auto& [stack, clip] : clips) {
            clip.name = names.Make(ClipName(*stack, clips.size() == 1), std::format("animacao{}", model.clips.size()));
            model.clips.push_back(std::move(clip));
        }
        return true;
    }

    const ufbx_scene& m_scene;
    fs::path m_baseDir;
    std::string m_fileStem;
    std::unordered_map<const ufbx_node*, int> m_nodeIndex;
    std::unordered_map<const ufbx_material*, int> m_materialIndex;
    std::unordered_map<const ufbx_texture*, int> m_images;
    UniqueNames m_imageNames;
    UniqueNames m_primitiveNames;
};

} // namespace

bool ParseFbx(std::span<const uint8_t> bytes, const fs::path& baseDir, ModelData& out, std::string* error,
              std::string_view fileStem) {
    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_COMPENSATE;
    opts.pivot_handling = UFBX_PIVOT_HANDLING_ADJUST_TO_PIVOT;
    opts.generate_missing_normals = true;
    opts.clean_skin_weights = true;
    opts.file_format = UFBX_FILE_FORMAT_FBX;

    ufbx_error loadError{};
    std::unique_ptr<ufbx_scene, SceneDeleter> scene(ufbx_load_memory(bytes.data(), bytes.size(), &opts, &loadError));
    if (!scene) {
        SetError(error, std::format("FBX: {}.", Str(loadError.description)));
        return false;
    }
    ModelData model;
    Converter converter(*scene, baseDir, std::string(fileStem));
    if (!converter.Run(model, error)) return false;
    out = std::move(model);
    return true;
}

bool LoadFbx(const fs::path& file, ModelData& out, std::string* error) {
    std::vector<uint8_t> bytes;
    if (!ReadBinaryFile(file, bytes)) {
        SetError(error, std::format("Não foi possível abrir '{}'.", ToUtf8(file)));
        return false;
    }
    std::string problem;
    if (!ParseFbx(bytes, file.parent_path(), out, &problem, ToUtf8(file.stem()))) {
        SetError(error, std::format("{}: {}", ToUtf8(file.filename()), problem));
        return false;
    }
    return true;
}

bool LoadModelFile(const fs::path& file, ModelData& out, std::string* error) {
    std::string ext = ToUtf8(file.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext == ".fbx" ? LoadFbx(file, out, error) : LoadGltf(file, out, error);
}

} // namespace motor
