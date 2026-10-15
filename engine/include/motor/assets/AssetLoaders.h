#pragma once

#include "motor/renderer/Mesh.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

// Tipos de arquivo que a engine sabe usar, reconhecidos pela extensão (sem diferenciar maiúsculas).
enum class AssetType { Unknown, Scene, Texture, Mesh };

AssetType AssetTypeFromPath(const std::filesystem::path& path);
const char* AssetTypeName(AssetType type);  // "Cena", "Textura", "Malha", "Arquivo"

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

} // namespace motor
