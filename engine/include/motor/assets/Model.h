#pragma once

// Modelos 3D glTF 2.0 (.gltf/.glb), Fase 11.
//
// LoadGltf lê o arquivo para ModelData (só CPU, testável sem janela): nós, malhas, materiais, imagens, peles e
// clipes. BuildModel transforma isso no asset Model que a cena usa: uma Mesh por primitiva (sub-asset do
// arquivo, referenciada como "<guid do arquivo>#<nome>"), o esqueleto (todos os nós) e os clipes.
// O prefab do modelo (o que vai para a cena) é gerado por ImportModel (motor/scene/ModelImport.h).

#include "motor/assets/Animation.h"
#include "motor/assets/AssetLoaders.h"
#include "motor/renderer/Skeleton.h"

#include <glm/vec4.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

class Mesh;

struct ModelImage {
    std::string name;
    std::filesystem::path file;   // imagem externa (caminho absoluto); vazio se embutida
    std::vector<uint8_t> bytes;   // imagem embutida (PNG/JPG como estava no arquivo)
    std::string mimeType;         // "image/png", "image/jpeg"
};

// Material PBR do glTF (metal/rugosidade), com as cores já em sRGB como no Material do Motor (Material.h).
// Imagens: índices em ModelData::images (-1 = sem).
struct ModelMaterial {
    std::string name;
    glm::vec4 color{1.0f};  // sRGB, como no Material (o glTF guarda linear)
    int image = -1;         // textura de cor
    float metallic = 1.0f;  // padrões do glTF
    float roughness = 1.0f;
    int metallicRoughnessImage = -1;  // G = rugosidade, B = metal (igual ao Motor)
    int normalImage = -1;
    float normalScale = 1.0f;
    int occlusionImage = -1;
    float occlusionStrength = 1.0f;
    glm::vec3 emissive{0.0f};  // sRGB
    float emissiveIntensity = 1.0f;  // KHR_materials_emissive_strength
    int emissiveImage = -1;
};

// Uma primitiva do glTF = uma malha com um material.
struct ModelPrimitive {
    std::string name;                 // único no arquivo: nome do sub-asset
    MeshData mesh;
    std::vector<SkinWeights> weights; // vazio se a malha não tiver pele
    int material = -1;
};

struct ModelNode {
    std::string name;  // único no arquivo (é também o nome da junta no esqueleto)
    int parent = -1;   // sempre menor que o índice do nó
    JointTransform local;
    std::vector<int> primitives;  // malha do nó (índices em ModelData::primitives)
    int skin = -1;
};

struct ModelSkin {
    std::vector<int> joints;  // índices de nós
    std::vector<glm::mat4> inverseBind;
};

struct ModelData {
    std::vector<ModelNode> nodes;  // pais antes dos filhos (só os da cena padrão do arquivo)
    std::vector<ModelPrimitive> primitives;
    std::vector<ModelMaterial> materials;
    std::vector<ModelImage> images;
    std::vector<ModelSkin> skins;
    std::vector<AnimationClip> clips;  // jointIndex = índice do nó; `skeleton` é preenchido por BuildModel

    bool IsJoint(int node) const;  // o nó é junta de alguma pele
};

// glTF 2.0: .gltf (JSON com .bin e imagens ao lado, ou embutidos em data URIs) e .glb.
bool LoadGltf(const std::filesystem::path& file, ModelData& out, std::string* error = nullptr);
// Mesmo, a partir dos bytes do arquivo; `baseDir` resolve os arquivos externos.
bool ParseGltf(std::span<const uint8_t> bytes, const std::filesystem::path& baseDir, ModelData& out,
               std::string* error = nullptr);

// Asset de modelo: o que fica em cache no AssetLibrary.
struct Model {
    struct NamedMesh {
        std::string name;
        std::shared_ptr<Mesh> mesh;
    };
    ModelData data;                     // descrição completa (o importador usa)
    std::vector<NamedMesh> meshes;      // uma por primitiva, na mesma ordem
    std::shared_ptr<Skeleton> skeleton; // um nó do arquivo = uma junta, na mesma ordem
    std::vector<std::shared_ptr<AnimationClip>> clips;

    std::shared_ptr<Mesh> FindMesh(std::string_view name) const;
    std::shared_ptr<AnimationClip> FindClip(std::string_view name) const;
};

// Cria as malhas (sem OpenGL: elas sobem para a GPU no primeiro desenho), o esqueleto e as peles.
std::shared_ptr<Model> BuildModel(ModelData data);

} // namespace motor
