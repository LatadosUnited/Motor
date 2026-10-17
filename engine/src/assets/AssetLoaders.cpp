#include "motor/assets/AssetLoaders.h"

#include "motor/core/Paths.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstring>
#include <format>
#include <fstream>
#include <unordered_map>

// stb_image: só os formatos que usamos, e sem stdio (lemos o arquivo nós mesmos, com caminho Unicode).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_HDR  // céus HDR (Fase 7)
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 16384
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace motor {

namespace fs = std::filesystem;

namespace {
void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

std::string LowerExtension(const fs::path& path) {
    std::string ext = ToUtf8(path.extension());
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

bool ReadFile(const fs::path& path, std::vector<uint8_t>& out, std::string* error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        SetError(error, std::format("Não foi possível abrir '{}'.", ToUtf8(path)));
        return false;
    }
    const std::streamsize size = file.tellg();
    file.seekg(0);
    out.resize(static_cast<size_t>(std::max<std::streamsize>(size, 0)));
    if (size > 0 && !file.read(reinterpret_cast<char*>(out.data()), size)) {
        SetError(error, std::format("Erro ao ler '{}'.", ToUtf8(path)));
        return false;
    }
    return true;
}

// --- OBJ -------------------------------------------------------------------

std::string_view Trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    const auto last = s.find_last_not_of(" \t\r");
    return s.substr(first, last - first + 1);
}

// Próximo token separado por espaços; avança `s`.
std::string_view NextToken(std::string_view& s) {
    const auto start = s.find_first_not_of(" \t");
    if (start == std::string_view::npos) {
        s = {};
        return {};
    }
    const auto end = s.find_first_of(" \t", start);
    const std::string_view token = s.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    s = end == std::string_view::npos ? std::string_view{} : s.substr(end);
    return token;
}

bool ParseFloat(std::string_view token, float& out) {
    if (!token.empty() && token[0] == '+') token.remove_prefix(1);
    const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), out);
    return ec == std::errc() && ptr == token.data() + token.size();
}

bool ParseInt(std::string_view token, int& out) {
    const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), out);
    return ec == std::errc() && ptr == token.data() + token.size();
}

template <int N>
bool ParseFloats(std::string_view rest, float* out, int required) {
    for (int i = 0; i < N; ++i) {
        const std::string_view token = NextToken(rest);
        if (token.empty()) return i >= required;
        if (!ParseFloat(token, out[i])) return false;
    }
    return true;
}

// Converte índice OBJ (1-based, ou negativo = relativo ao fim) para 0-based. -1 se inválido.
int ResolveIndex(int index, size_t count) {
    if (index > 0 && static_cast<size_t>(index) <= count) return index - 1;
    if (index < 0 && static_cast<size_t>(-index) <= count) return static_cast<int>(count) + index;
    return -1;
}

struct VertexKey {
    int position, uv, normal;
    bool operator==(const VertexKey&) const = default;
};

struct VertexKeyHash {
    size_t operator()(const VertexKey& k) const {
        size_t h = std::hash<int>()(k.position);
        h ^= std::hash<int>()(k.uv) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(k.normal) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
} // namespace

AssetType AssetTypeFromPath(const fs::path& path) {
    const std::string ext = LowerExtension(path);
    if (ext == kSceneFileExtension) return AssetType::Scene;
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") return AssetType::Texture;
    if (ext == ".obj") return AssetType::Mesh;
    if (ext == kMaterialFileExtension) return AssetType::Material;
    if (ext == kPrefabFileExtension) return AssetType::Prefab;
    if (ext == ".ttf" || ext == ".otf") return AssetType::Font;  // fontes da UI do jogo (motor/ui/Font.h)
    if (ext == ".gltf" || ext == ".glb") return AssetType::Model;
    if (ext == ".hdr") return AssetType::EnvironmentMap;  // céu equirretangular (Fase 7)
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") return AssetType::Audio;  // clipes de som (Fase 8)
    return AssetType::Unknown;
}

const char* AssetTypeName(AssetType type) {
    switch (type) {
        case AssetType::Scene: return "Cena";
        case AssetType::Texture: return "Textura";
        case AssetType::Mesh: return "Malha";
        case AssetType::Material: return "Material";
        case AssetType::Prefab: return "Prefab";
        case AssetType::Font: return "Fonte";
        case AssetType::Model: return "Modelo";
        case AssetType::EnvironmentMap: return "Céu HDR";
        case AssetType::Audio: return "Som";
        case AssetType::Unknown: break;
    }
    return "Arquivo";
}

bool ParseObj(std::string_view text, MeshData& out, std::string* error) {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec3> normals;
    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexMap;
    std::vector<int> vertexPosition;  // índice de posição de cada vértice gerado (para normais calculadas)
    bool missingNormals = false;

    out.vertices.clear();
    out.indices.clear();

    int lineNumber = 0;
    while (!text.empty()) {
        ++lineNumber;
        const size_t eol = text.find('\n');
        std::string_view line = text.substr(0, eol);
        text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);

        if (const size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        line = Trim(line);
        if (line.empty()) continue;

        std::string_view rest = line;
        const std::string_view keyword = NextToken(rest);
        auto fail = [&](std::string_view what) {
            SetError(error, std::format("OBJ inválido na linha {}: {}", lineNumber, what));
            return false;
        };

        if (keyword == "v") {
            glm::vec3 p{0.0f};
            if (!ParseFloats<3>(rest, &p.x, 3)) return fail("posição (v) malformada");
            positions.push_back(p);
        } else if (keyword == "vt") {
            glm::vec2 uv{0.0f};
            if (!ParseFloats<2>(rest, &uv.x, 1)) return fail("coordenada de textura (vt) malformada");
            uvs.push_back(uv);
        } else if (keyword == "vn") {
            glm::vec3 n{0.0f};
            if (!ParseFloats<3>(rest, &n.x, 3)) return fail("normal (vn) malformada");
            normals.push_back(n);
        } else if (keyword == "f") {
            std::vector<uint32_t> polygon;
            for (std::string_view token = NextToken(rest); !token.empty(); token = NextToken(rest)) {
                // v, v/t, v//n ou v/t/n
                std::array<std::string_view, 3> parts{};
                size_t partIndex = 0, start = 0;
                for (size_t i = 0; i <= token.size(); ++i) {
                    if (i == token.size() || token[i] == '/') {
                        if (partIndex >= parts.size()) return fail("vértice de face com partes demais");
                        parts[partIndex++] = token.substr(start, i - start);
                        start = i + 1;
                    }
                }

                VertexKey key{-1, -1, -1};
                int raw = 0;
                if (!ParseInt(parts[0], raw) || (key.position = ResolveIndex(raw, positions.size())) < 0) {
                    return fail("índice de posição fora do intervalo");
                }
                if (!parts[1].empty() && (!ParseInt(parts[1], raw) || (key.uv = ResolveIndex(raw, uvs.size())) < 0)) {
                    return fail("índice de textura fora do intervalo");
                }
                if (!parts[2].empty() &&
                    (!ParseInt(parts[2], raw) || (key.normal = ResolveIndex(raw, normals.size())) < 0)) {
                    return fail("índice de normal fora do intervalo");
                }

                auto [it, inserted] = vertexMap.try_emplace(key, static_cast<uint32_t>(out.vertices.size()));
                if (inserted) {
                    Vertex v{};
                    v.position = positions[static_cast<size_t>(key.position)];
                    v.uv = key.uv >= 0 ? uvs[static_cast<size_t>(key.uv)] : glm::vec2(0.0f);
                    v.normal = key.normal >= 0 ? normals[static_cast<size_t>(key.normal)] : glm::vec3(0.0f);
                    if (key.normal < 0) missingNormals = true;
                    out.vertices.push_back(v);
                    vertexPosition.push_back(key.position);
                }
                polygon.push_back(it->second);
            }
            if (polygon.size() < 3) return fail("face com menos de 3 vértices");
            for (size_t i = 1; i + 1 < polygon.size(); ++i) {
                out.indices.insert(out.indices.end(), {polygon[0], polygon[i], polygon[i + 1]});
            }
        }
        // o, g, s, mtllib, usemtl, l, p...: ignorados.
    }

    if (out.indices.empty()) {
        SetError(error, "O arquivo OBJ não tem faces.");
        return false;
    }

    if (missingNormals) {
        // Normais suaves por posição: soma das normais das faces (ponderadas pela área) que usam cada posição.
        std::vector<glm::vec3> accum(positions.size(), glm::vec3(0.0f));
        for (size_t i = 0; i + 2 < out.indices.size(); i += 3) {
            const uint32_t a = out.indices[i], b = out.indices[i + 1], c = out.indices[i + 2];
            const glm::vec3 faceNormal = glm::cross(out.vertices[b].position - out.vertices[a].position,
                                                    out.vertices[c].position - out.vertices[a].position);
            for (uint32_t v : {a, b, c}) accum[static_cast<size_t>(vertexPosition[v])] += faceNormal;
        }
        for (size_t v = 0; v < out.vertices.size(); ++v) {
            if (glm::length(out.vertices[v].normal) > 0.0f) continue;  // veio do arquivo
            const glm::vec3 n = accum[static_cast<size_t>(vertexPosition[v])];
            out.vertices[v].normal = glm::length(n) > 1e-12f ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }
    return true;
}

bool LoadObj(const fs::path& path, MeshData& out, std::string* error) {
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes, error)) return false;
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!ParseObj(text, out, error)) {
        if (error) *error = std::format("{}: {}", ToUtf8(path.filename()), *error);
        return false;
    }
    return true;
}

bool DecodeImage(std::span<const uint8_t> bytes, ImageData& out, std::string* error) {
    if (bytes.empty() || bytes.size() > static_cast<size_t>(INT32_MAX)) {
        SetError(error, "Imagem vazia ou grande demais.");
        return false;
    }
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (!pixels) {
        SetError(error, std::format("Imagem inválida ou formato não suportado ({}).", stbi_failure_reason()));
        return false;
    }

    out.width = width;
    out.height = height;
    out.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    // Arquivos guardam a linha de cima primeiro; o OpenGL espera a de baixo.
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; ++y) {
        std::memcpy(out.pixels.data() + static_cast<size_t>(height - 1 - y) * rowBytes,
                    pixels + static_cast<size_t>(y) * rowBytes, rowBytes);
    }
    stbi_image_free(pixels);
    return true;
}

bool DecodeHdrImage(std::span<const uint8_t> bytes, HdrImage& out, std::string* error) {
    if (bytes.empty() || bytes.size() > static_cast<size_t>(INT32_MAX)) {
        SetError(error, "Imagem vazia ou grande demais.");
        return false;
    }
    int width = 0, height = 0, channels = 0;
    float* pixels = stbi_loadf_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 3);
    if (!pixels) {
        SetError(error, std::format("Imagem HDR inválida ({}).", stbi_failure_reason()));
        return false;
    }
    out.width = width;
    out.height = height;
    // Linha de cima primeiro, como no arquivo (é o que a amostragem equirretangular espera).
    out.rgb.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
    stbi_image_free(pixels);
    return true;
}

bool LoadHdrFile(const fs::path& path, HdrImage& out, std::string* error) {
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes, error)) return false;
    if (!DecodeHdrImage(bytes, out, error)) {
        if (error) *error = std::format("{}: {}", ToUtf8(path.filename()), *error);
        return false;
    }
    return true;
}

bool LoadImageFile(const fs::path& path, ImageData& out, std::string* error) {
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes, error)) return false;
    if (!DecodeImage(bytes, out, error)) {
        if (error) *error = std::format("{}: {}", ToUtf8(path.filename()), *error);
        return false;
    }
    return true;
}

} // namespace motor
