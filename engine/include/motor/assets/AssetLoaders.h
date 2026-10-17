#pragma once

#include "motor/renderer/Environment.h"
#include "motor/renderer/Mesh.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

// Tipos de arquivo que a engine sabe usar, reconhecidos pela extensão (sem diferenciar maiúsculas).
// Model: glTF 2.0 (.gltf/.glb), motor/assets/Model.h. Font: .ttf/.otf da UI do jogo (motor/ui/Font.h).
// EnvironmentMap: céu em HDR equirretangular (.hdr), usado no ambiente da cena (Fase 7).
enum class AssetType { Unknown, Scene, Texture, Mesh, Material, Prefab, Font, Model, EnvironmentMap };

// Extensão dos arquivos de cena. Fica aqui (camada de assets) para o tipo do arquivo ser reconhecido sem
// depender da cena; SceneSerializer::kFileExtension é a mesma.
inline constexpr const char* kSceneFileExtension = ".mscene";
inline constexpr const char* kMaterialFileExtension = ".mmat";   // MaterialFile.h
inline constexpr const char* kPrefabFileExtension = ".mprefab";  // motor/scene/Prefab.h

AssetType AssetTypeFromPath(const std::filesystem::path& path);
const char* AssetTypeName(AssetType type);  // "Cena", "Textura", "Malha", "Material", "Prefab", "Fonte", "Modelo", "Céu HDR", "Arquivo"

// Dados na CPU, antes de irem para a GPU. Estas funções não usam OpenGL (dá para testar sem janela).
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct ImageData {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  // RGBA 8 bits, linha 0 = base da imagem (convenção do OpenGL)
};

// Wavefront OBJ: v, vt, vn e f (polígonos viram leque de triângulos, índices negativos aceitos).
// Sem normais no arquivo, calcula normais suaves. Materiais (mtllib/usemtl) são ignorados.
bool ParseObj(std::string_view text, MeshData& out, std::string* error = nullptr);
bool LoadObj(const std::filesystem::path& path, MeshData& out, std::string* error = nullptr);

// PNG, JPG, BMP e TGA (via stb_image). Sempre converte para RGBA.
bool DecodeImage(std::span<const uint8_t> bytes, ImageData& out, std::string* error = nullptr);
bool LoadImageFile(const std::filesystem::path& path, ImageData& out, std::string* error = nullptr);

// Radiance HDR (.hdr, via stb_image): RGB float linear, linha de cima primeiro (motor::HdrImage).
bool DecodeHdrImage(std::span<const uint8_t> bytes, HdrImage& out, std::string* error = nullptr);
bool LoadHdrFile(const std::filesystem::path& path, HdrImage& out, std::string* error = nullptr);

} // namespace motor
