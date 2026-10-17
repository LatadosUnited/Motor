#pragma once

#include <glm/vec3.hpp>

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace motor {

// Imagem HDR na CPU (céu equirretangular, .hdr): RGB float linear, width*height*3 valores, linha 0 = topo da
// imagem (como no arquivo). Os assets carregam (AssetLibrary::GetEnvironmentMap); o renderer sobe para a GPU.
struct HdrImage {
    int width = 0;
    int height = 0;
    std::vector<float> rgb;

    glm::vec3 Pixel(int x, int y) const {
        const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3;
        return {rgb[i], rgb[i + 1], rgb[i + 2]};
    }
};

// A ordem é a do seletor no editor e o número aceito na cena: só acrescente no fim.
enum class SkyMode { Color = 0, Texture = 1 };       // céu de cor (degradê) ou imagem HDR
enum class ToneMapper { Aces = 0, Reinhard = 1, None = 2 };

// Ambiente da cena: céu, luz ambiente, neblina, sombras e pós-processamento.
struct Environment {
    glm::vec3 skyColor{0.53f, 0.70f, 0.88f};  // sRGB; cor do céu (modo cor) e da neblina
    float fogDensity = 0.012f;
    glm::vec3 ambient{0.12f, 0.14f, 0.18f};   // luz extra que chega a tudo, sem direção (linear)

    // Céu (Fase 7). Também ilumina a cena: luz difusa (irradiância) e reflexos (mapa pré-filtrado).
    SkyMode skyMode = SkyMode::Color;
    std::shared_ptr<const HdrImage> skyTexture;  // modo Texture: imagem equirretangular (nulo = usa a cor)
    float skyIntensity = 1.0f;   // brilho do céu ao fundo e da luz que ele dá
    float skyRotation = 0.0f;    // graus em torno do Y
    float skyLight = 1.0f;       // quanto o céu ilumina a cena (difusa e reflexos); 0 = só `ambient`

    // Sombras: até onde a luz direcional projeta sombras (metros a partir da câmera).
    float shadowDistance = 60.0f;

    // Pós-processamento (a cena é desenhada em HDR).
    float exposure = 1.0f;
    ToneMapper toneMapper = ToneMapper::Aces;
    bool bloom = true;
    float bloomIntensity = 0.5f;
    float bloomThreshold = 1.0f;  // brilho (linear) a partir do qual a luz "vaza"
    bool fxaa = true;

    bool operator==(const Environment&) const = default;
};

// Nomes gravados na cena (e aceitos pelo MCP). Parse*: nome desconhecido deixa o valor como está.
inline const char* SkyModeName(SkyMode mode) { return mode == SkyMode::Texture ? "Texture" : "Color"; }
inline const char* ToneMapperName(ToneMapper t) {
    return t == ToneMapper::Reinhard ? "Reinhard" : t == ToneMapper::None ? "None" : "ACES";
}
inline bool ParseSkyMode(std::string_view name, SkyMode& out) {
    if (name == "Color") out = SkyMode::Color;
    else if (name == "Texture") out = SkyMode::Texture;
    else return false;
    return true;
}
inline bool ParseToneMapper(std::string_view name, ToneMapper& out) {
    if (name == "ACES") out = ToneMapper::Aces;
    else if (name == "Reinhard") out = ToneMapper::Reinhard;
    else if (name == "None") out = ToneMapper::None;
    else return false;
    return true;
}

} // namespace motor
