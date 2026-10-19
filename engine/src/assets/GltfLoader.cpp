// Leitura de glTF 2.0 com o cgltf (Fase 11). Converte para ModelData: nós em ordem (pai antes do filho), uma
// malha por primitiva, materiais no modelo de iluminação do Motor, imagens, peles e clipes.
#include "motor/assets/Model.h"

#include "motor/core/Paths.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace motor {

namespace fs = std::filesystem;

namespace {

void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

const char* ResultText(cgltf_result r) {
    switch (r) {
        case cgltf_result_success: return "ok";
        case cgltf_result_data_too_short: return "arquivo cortado";
        case cgltf_result_unknown_format: return "não é glTF";
        case cgltf_result_invalid_json: return "JSON inválido";
        case cgltf_result_invalid_gltf: return "glTF inválido";
        case cgltf_result_invalid_options: return "opções inválidas";
        case cgltf_result_file_not_found: return "arquivo externo não encontrado";
        case cgltf_result_io_error: return "erro de leitura";
        case cgltf_result_out_of_memory: return "memória insuficiente";
        case cgltf_result_legacy_gltf: return "glTF 1.0 não é suportado";
        default: return "erro desconhecido";
    }
}

bool ReadBytes(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamsize size = file.tellg();
    file.seekg(0);
    out.resize(static_cast<size_t>(std::max<std::streamsize>(size, 0)));
    return size <= 0 || static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
}

// Arquivos externos (.bin) pelo caminho UTF-8: o fopen padrão do cgltf não abre caminhos com acento no Windows.
cgltf_result ReadExternal(const cgltf_memory_options*, const cgltf_file_options*, const char* path, cgltf_size* size,
                          void** data) {
    std::vector<uint8_t> bytes;
    if (!ReadBytes(PathFromUtf8(path), bytes)) return cgltf_result_file_not_found;
    void* copy = std::malloc(std::max<size_t>(bytes.size(), 1));
    if (!copy) return cgltf_result_out_of_memory;
    if (!bytes.empty()) std::memcpy(copy, bytes.data(), bytes.size());
    *size = bytes.size();
    *data = copy;
    return cgltf_result_success;
}

void ReleaseExternal(const cgltf_memory_options*, const cgltf_file_options*, void* data) {
    std::free(data);
}

struct CgltfDeleter {
    void operator()(cgltf_data* d) const { cgltf_free(d); }
};

// Nomes únicos: vazio vira "<prefixo><i>", repetido ganha " (2)", " (3)"...
class UniqueNames {
public:
    std::string Make(const char* name, const std::string& fallback) {
        std::string base = name && *name ? name : fallback;
        std::string result = base;
        for (int n = 2; m_used.contains(result); ++n) result = std::format("{} ({})", base, n);
        m_used.insert(result);
        return result;
    }

private:
    std::set<std::string> m_used;
};

std::vector<glm::vec4> ReadVec(const cgltf_accessor* a, int components) {
    std::vector<glm::vec4> out(a ? a->count : 0, glm::vec4(0.0f));
    for (size_t i = 0; i < out.size(); ++i) {
        cgltf_accessor_read_float(a, i, glm::value_ptr(out[i]), static_cast<cgltf_size>(components));
    }
    return out;
}

// Normais suaves (média das faces) quando o arquivo não traz.
void ComputeNormals(MeshData& m) {
    for (Vertex& v : m.vertices) v.normal = glm::vec3(0.0f);
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        Vertex& a = m.vertices[m.indices[i]];
        Vertex& b = m.vertices[m.indices[i + 1]];
        Vertex& c = m.vertices[m.indices[i + 2]];
        const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
        a.normal += n;
        b.normal += n;
        c.normal += n;
    }
    for (Vertex& v : m.vertices) {
        const float len = glm::length(v.normal);
        v.normal = len > 1e-12f ? v.normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

bool ReadPrimitive(const cgltf_primitive& p, ModelPrimitive& out, std::string& problem) {
    if (p.type != cgltf_primitive_type_triangles) {
        problem = "primitiva que não é de triângulos (ignorada)";
        return false;
    }
    const cgltf_accessor *position = nullptr, *normal = nullptr, *uv = nullptr, *joints = nullptr, *weights = nullptr;
    for (size_t i = 0; i < p.attributes_count; ++i) {
        const cgltf_attribute& a = p.attributes[i];
        if (a.type == cgltf_attribute_type_position) position = a.data;
        else if (a.type == cgltf_attribute_type_normal) normal = a.data;
        else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uv = a.data;
        else if (a.type == cgltf_attribute_type_joints && a.index == 0) joints = a.data;
        else if (a.type == cgltf_attribute_type_weights && a.index == 0) weights = a.data;
    }
    if (!position || position->count == 0) {
        problem = "primitiva sem posições (ignorada)";
        return false;
    }

    const size_t count = position->count;
    const auto positions = ReadVec(position, 3);
    const auto normals = normal && normal->count == count ? ReadVec(normal, 3) : std::vector<glm::vec4>{};
    const auto uvs = uv && uv->count == count ? ReadVec(uv, 2) : std::vector<glm::vec4>{};

    out.mesh.vertices.resize(count);
    for (size_t i = 0; i < count; ++i) {
        Vertex& v = out.mesh.vertices[i];
        v.position = glm::vec3(positions[i]);
        v.normal = normals.empty() ? glm::vec3(0.0f) : glm::vec3(normals[i]);
        // glTF: (0, 0) é o canto de cima da imagem; as nossas texturas têm a linha 0 embaixo.
        v.uv = uvs.empty() ? glm::vec2(0.0f) : glm::vec2(uvs[i].x, 1.0f - uvs[i].y);
    }

    if (p.indices) {
        out.mesh.indices.resize(p.indices->count);
        for (size_t i = 0; i < p.indices->count; ++i) {
            const size_t index = cgltf_accessor_read_index(p.indices, i);
            if (index >= count) {
                problem = "índice fora da malha";
                return false;
            }
            out.mesh.indices[i] = static_cast<uint32_t>(index);
        }
    } else {
        out.mesh.indices.resize(count);
        for (size_t i = 0; i < count; ++i) out.mesh.indices[i] = static_cast<uint32_t>(i);
    }
    out.mesh.indices.resize(out.mesh.indices.size() / 3 * 3);
    if (normals.empty()) ComputeNormals(out.mesh);

    if (joints && weights && joints->count == count && weights->count == count) {
        const auto js = ReadVec(joints, 4);
        const auto ws = ReadVec(weights, 4);
        out.weights.resize(count);
        for (size_t i = 0; i < count; ++i) {
            glm::vec4 w = glm::max(ws[i], glm::vec4(0.0f));
            const float sum = w.x + w.y + w.z + w.w;
            w = sum > 1e-6f ? w / sum : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            out.weights[i] = {js[i], w};
        }
    }
    return true;
}

int ImageOf(const cgltf_data& d, const cgltf_texture_view& view) {
    return view.texture && view.texture->image ? static_cast<int>(cgltf_image_index(&d, view.texture->image)) : -1;
}

glm::vec3 LinearToSrgb(const glm::vec3& linear) {
    return glm::pow(glm::max(linear, glm::vec3(0.0f)), glm::vec3(1.0f / 2.2f));
}

// Material PBR (metal/rugosidade): o mesmo modelo do Motor desde a Fase 7.
ModelMaterial ReadMaterial(const cgltf_data& d, const cgltf_material& m, const std::string& name) {
    ModelMaterial out;
    out.name = name;
    if (m.has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness& pbr = m.pbr_metallic_roughness;
        const glm::vec3 linear{pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]};
        out.color = glm::vec4(LinearToSrgb(linear), pbr.base_color_factor[3]);
        out.metallic = std::clamp(pbr.metallic_factor, 0.0f, 1.0f);
        out.roughness = std::clamp(pbr.roughness_factor, 0.0f, 1.0f);
        out.image = ImageOf(d, pbr.base_color_texture);
        out.metallicRoughnessImage = ImageOf(d, pbr.metallic_roughness_texture);
    }
    out.normalImage = ImageOf(d, m.normal_texture);
    if (out.normalImage >= 0) out.normalScale = m.normal_texture.scale;
    out.occlusionImage = ImageOf(d, m.occlusion_texture);
    if (out.occlusionImage >= 0) out.occlusionStrength = std::clamp(m.occlusion_texture.scale, 0.0f, 1.0f);
    out.emissive = LinearToSrgb(glm::make_vec3(m.emissive_factor));
    out.emissiveImage = ImageOf(d, m.emissive_texture);
    if (m.has_emissive_strength) out.emissiveIntensity = std::max(m.emissive_strength.emissive_strength, 0.0f);
    out.blend = m.alpha_mode == cgltf_alpha_mode_mask    ? BlendMode::Cutout
                : m.alpha_mode == cgltf_alpha_mode_blend ? BlendMode::Transparent
                                                         : BlendMode::Opaque;
    out.alphaCutoff = std::clamp(m.alpha_cutoff, 0.0f, 1.0f);
    out.doubleSided = m.double_sided;
    return out;
}

bool ReadImage(const cgltf_options& options, const cgltf_image& image, const fs::path& baseDir, size_t index,
               ModelImage& out) {
    out.mimeType = image.mime_type ? image.mime_type : "";
    if (image.buffer_view) {
        const cgltf_buffer_view& view = *image.buffer_view;
        const uint8_t* data = static_cast<const uint8_t*>(view.buffer->data);
        if (!data || view.offset + view.size > view.buffer->size) return false;
        out.bytes.assign(data + view.offset, data + view.offset + view.size);
    } else if (image.uri && std::strncmp(image.uri, "data:", 5) == 0) {
        const char* comma = std::strchr(image.uri, ',');
        if (!comma || !std::strstr(image.uri, ";base64")) return false;
        if (out.mimeType.empty()) out.mimeType.assign(image.uri + 5, std::strcspn(image.uri + 5, ";,"));
        const char* base64 = comma + 1;
        const size_t length = std::strlen(base64);
        size_t padding = 0;
        while (padding < length && padding < 2 && base64[length - 1 - padding] == '=') ++padding;
        const size_t size = length / 4 * 3 - padding;
        void* decoded = nullptr;
        if (cgltf_load_buffer_base64(&options, size, base64, &decoded) != cgltf_result_success) return false;
        out.bytes.assign(static_cast<uint8_t*>(decoded), static_cast<uint8_t*>(decoded) + size);
        std::free(decoded);
    } else if (image.uri) {
        std::string uri = image.uri;
        uri.resize(cgltf_decode_uri(uri.data()));
        out.file = (baseDir / PathFromUtf8(uri)).lexically_normal();
    } else {
        return false;
    }
    std::string name = image.name ? image.name : "";
    if (name.empty() && !out.file.empty()) name = ToUtf8(out.file.stem());
    out.name = name.empty() ? std::format("imagem{}", index) : name;
    return true;
}

// Materiais e imagens. Imagem sem dados sai dos materiais que a usavam.
void ReadMaterialsAndImages(const cgltf_options& options, const cgltf_data& d, const fs::path& baseDir,
                            ModelData& model) {
    UniqueNames materialNames;
    for (size_t i = 0; i < d.materials_count; ++i) {
        model.materials.push_back(ReadMaterial(d, d.materials[i], materialNames.Make(d.materials[i].name, std::format("material{}", i))));
    }
    UniqueNames imageNames;
    for (size_t i = 0; i < d.images_count; ++i) {
        ModelImage image;
        if (!ReadImage(options, d.images[i], baseDir, i, image)) image.name = std::format("imagem{}", i);  // sem dados
        image.name = imageNames.Make(image.name.c_str(), std::format("imagem{}", i));
        model.images.push_back(std::move(image));
    }
    for (ModelMaterial& m : model.materials) {
        for (int* image : {&m.image, &m.metallicRoughnessImage, &m.normalImage, &m.occlusionImage, &m.emissiveImage}) {
            if (*image >= 0 && model.images[static_cast<size_t>(*image)].bytes.empty() &&
                model.images[static_cast<size_t>(*image)].file.empty()) {
                *image = -1;  // imagem sem dados
            }
        }
    }
}

// Malhas: uma primitiva = uma malha. Devolve, para cada malha do glTF, os índices das suas primitivas.
std::vector<std::vector<int>> ReadMeshes(const cgltf_data& d, ModelData& model) {
    std::vector<std::vector<int>> primitivesOfMesh(d.meshes_count);
    UniqueNames meshNames;
    for (size_t mi = 0; mi < d.meshes_count; ++mi) {
        const cgltf_mesh& mesh = d.meshes[mi];
        const std::string base = mesh.name && *mesh.name ? mesh.name : std::format("malha{}", mi);
        for (size_t pi = 0; pi < mesh.primitives_count; ++pi) {
            ModelPrimitive prim;
            std::string problem;
            if (!ReadPrimitive(mesh.primitives[pi], prim, problem)) continue;
            const std::string name = mesh.primitives_count > 1 ? std::format("{}.{}", base, pi) : base;
            prim.name = meshNames.Make(name.c_str(), name);
            if (mesh.primitives[pi].material) {
                prim.material = static_cast<int>(cgltf_material_index(&d, mesh.primitives[pi].material));
            }
            primitivesOfMesh[mi].push_back(static_cast<int>(model.primitives.size()));
            model.primitives.push_back(std::move(prim));
        }
    }
    return primitivesOfMesh;
}

// Nós da cena padrão, pai antes do filho. Devolve nó do glTF -> índice em model.nodes.
std::unordered_map<const cgltf_node*, int> ReadNodes(const cgltf_data& d, const std::vector<std::vector<int>>& primitivesOfMesh,
                                                     ModelData& model) {
    std::unordered_map<const cgltf_node*, int> nodeIndex;
    UniqueNames nodeNames;
    auto addNode = [&](auto& self, const cgltf_node* node, int parent) -> void {
        if (nodeIndex.contains(node)) return;  // nó em duas cenas/duas vezes: o primeiro vale
        ModelNode n;
        n.name = nodeNames.Make(node->name, std::format("no{}", cgltf_node_index(&d, node)));
        n.parent = parent;
        if (node->has_matrix) {
            const glm::mat4 m = glm::make_mat4(node->matrix);
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(m, n.local.scale, n.local.rotation, n.local.translation, skew, perspective);
            n.local.rotation = glm::normalize(n.local.rotation);
        } else {
            if (node->has_translation) n.local.translation = glm::make_vec3(node->translation);
            if (node->has_rotation) {
                n.local.rotation = glm::normalize(glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]));
            }
            if (node->has_scale) n.local.scale = glm::make_vec3(node->scale);
        }
        if (node->mesh) n.primitives = primitivesOfMesh[cgltf_mesh_index(&d, node->mesh)];
        if (node->skin) n.skin = static_cast<int>(cgltf_skin_index(&d, node->skin));
        const int index = static_cast<int>(model.nodes.size());
        nodeIndex[node] = index;
        model.nodes.push_back(std::move(n));
        for (size_t i = 0; i < node->children_count; ++i) self(self, node->children[i], index);
    };
    const cgltf_scene* scene = d.scene ? d.scene : (d.scenes_count > 0 ? &d.scenes[0] : nullptr);
    if (scene) {
        for (size_t i = 0; i < scene->nodes_count; ++i) addNode(addNode, scene->nodes[i], -1);
    } else {
        for (size_t i = 0; i < d.nodes_count; ++i) {
            if (!d.nodes[i].parent) addNode(addNode, &d.nodes[i], -1);
        }
    }
    return nodeIndex;
}

void ReadSkins(const cgltf_data& d, const std::unordered_map<const cgltf_node*, int>& nodeIndex, ModelData& model) {
    for (size_t si = 0; si < d.skins_count; ++si) {
        const cgltf_skin& s = d.skins[si];
        ModelSkin skin;
        for (size_t j = 0; j < s.joints_count; ++j) {
            const auto it = nodeIndex.find(s.joints[j]);
            skin.joints.push_back(it == nodeIndex.end() ? -1 : it->second);
            glm::mat4 ibm(1.0f);
            if (s.inverse_bind_matrices) cgltf_accessor_read_float(s.inverse_bind_matrices, j, glm::value_ptr(ibm), 16);
            skin.inverseBind.push_back(ibm);
        }
        model.skins.push_back(std::move(skin));
    }
}

// Clipes de animação (translação, rotação e escala; pesos de morph ficam de fora).
void ReadClips(const cgltf_data& d, const std::unordered_map<const cgltf_node*, int>& nodeIndex, ModelData& model) {
    UniqueNames clipNames;
    for (size_t ai = 0; ai < d.animations_count; ++ai) {
        const cgltf_animation& a = d.animations[ai];
        AnimationClip clip;
        clip.name = clipNames.Make(a.name, std::format("animacao{}", ai));
        for (size_t ci = 0; ci < a.channels_count; ++ci) {
            const cgltf_animation_channel& c = a.channels[ci];
            const auto node = nodeIndex.find(c.target_node);
            if (!c.sampler || node == nodeIndex.end()) continue;
            AnimationChannel channel;
            switch (c.target_path) {
                case cgltf_animation_path_type_translation: channel.path = AnimationPath::Translation; break;
                case cgltf_animation_path_type_rotation: channel.path = AnimationPath::Rotation; break;
                case cgltf_animation_path_type_scale: channel.path = AnimationPath::Scale; break;
                default: continue;  // pesos de morph: não suportado
            }
            switch (c.sampler->interpolation) {
                case cgltf_interpolation_type_step: channel.interpolation = AnimationInterpolation::Step; break;
                case cgltf_interpolation_type_cubic_spline: channel.interpolation = AnimationInterpolation::CubicSpline; break;
                default: channel.interpolation = AnimationInterpolation::Linear; break;
            }
            channel.jointIndex = node->second;
            channel.joint = model.nodes[static_cast<size_t>(node->second)].name;
            const cgltf_accessor* input = c.sampler->input;
            channel.times.resize(input->count);
            for (size_t k = 0; k < input->count; ++k) cgltf_accessor_read_float(input, k, &channel.times[k], 1);
            channel.values = ReadVec(c.sampler->output, channel.path == AnimationPath::Rotation ? 4 : 3);
            const size_t stride = channel.interpolation == AnimationInterpolation::CubicSpline ? 3 : 1;
            if (channel.times.empty() || channel.values.size() < channel.times.size() * stride) continue;
            if (!std::is_sorted(channel.times.begin(), channel.times.end())) continue;
            clip.duration = std::max(clip.duration, channel.times.back());
            clip.channels.push_back(std::move(channel));
        }
        model.clips.push_back(std::move(clip));
    }
}

} // namespace

bool ParseGltf(std::span<const uint8_t> bytes, const fs::path& baseDir, ModelData& out, std::string* error) {
    cgltf_options options{};
    options.file.read = &ReadExternal;
    options.file.release = &ReleaseExternal;

    cgltf_data* raw = nullptr;
    cgltf_result r = cgltf_parse(&options, bytes.data(), bytes.size(), &raw);
    std::unique_ptr<cgltf_data, CgltfDeleter> data(raw);
    if (r != cgltf_result_success) {
        SetError(error, std::format("glTF: {}.", ResultText(r)));
        return false;
    }
    // O cgltf junta o diretório do "arquivo" com o URI: passamos um nome qualquer dentro de baseDir.
    const std::string gltfPath = ToGenericUtf8(baseDir / "modelo.gltf");
    r = cgltf_load_buffers(&options, data.get(), gltfPath.c_str());
    if (r != cgltf_result_success) {
        SetError(error, std::format("glTF: buffers não carregados ({}).", ResultText(r)));
        return false;
    }
    r = cgltf_validate(data.get());
    if (r != cgltf_result_success) {
        SetError(error, std::format("glTF: {}.", ResultText(r)));
        return false;
    }
    const cgltf_data& d = *data;
    ModelData model;
    ReadMaterialsAndImages(options, d, baseDir, model);
    const std::vector<std::vector<int>> primitivesOfMesh = ReadMeshes(d, model);
    const std::unordered_map<const cgltf_node*, int> nodeIndex = ReadNodes(d, primitivesOfMesh, model);
    ReadSkins(d, nodeIndex, model);
    ReadClips(d, nodeIndex, model);

    if (model.primitives.empty() && model.nodes.empty()) {
        SetError(error, "glTF sem malhas nem nós.");
        return false;
    }
    out = std::move(model);
    return true;
}

bool LoadGltf(const fs::path& file, ModelData& out, std::string* error) {
    std::vector<uint8_t> bytes;
    if (!ReadBytes(file, bytes)) {
        SetError(error, std::format("Não foi possível abrir '{}'.", ToUtf8(file)));
        return false;
    }
    std::string problem;
    if (!ParseGltf(bytes, file.parent_path(), out, &problem)) {
        SetError(error, std::format("{}: {}", ToUtf8(file.filename()), problem));
        return false;
    }
    return true;
}

} // namespace motor
