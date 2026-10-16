#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Imagens para o servidor MCP (captura do viewport): PNG mínimo e base64, sem biblioteca.
namespace editor::mcp {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;  // linhas de cima para baixo
};

// Reduz por média de blocos até caber em `maxWidth` x `maxHeight` (mantém a proporção; não aumenta).
Image Downscale(const Image& image, int maxWidth, int maxHeight);
// PNG RGB de 8 bits (o alfa é descartado), com filtros por linha e deflate próprio (Huffman fixo + LZ77).
std::vector<uint8_t> EncodePng(const Image& image);
std::string Base64(const std::vector<uint8_t>& data);

} // namespace editor::mcp
