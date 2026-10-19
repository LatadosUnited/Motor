#pragma once

#include "motor/renderer/Texture.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace motor {

// Como o alfa da cor (cor x textura) é usado (Fase 18):
//   Opaque       ignora o alfa;
//   Cutout       recorte: some onde o alfa fica abaixo de alphaCutoff (folhas, grades); faz sombra recortada;
//   Transparent  mistura com o que está atrás (vidro, água simples); desenhado depois dos opacos, de trás para
//                frente, sem sombra.
enum class BlendMode { Opaque, Cutout, Transparent };

inline const char* BlendModeName(BlendMode mode) {  // gravado na cena e no .mmat
    switch (mode) {
        case BlendMode::Opaque: return "Opaque";
        case BlendMode::Cutout: return "Cutout";
        case BlendMode::Transparent: return "Transparent";
    }
    return "Opaque";
}

inline const char* BlendModeLabel(BlendMode mode) {  // no editor
    switch (mode) {
        case BlendMode::Opaque: return "Opaco";
        case BlendMode::Cutout: return "Recorte";
        case BlendMode::Transparent: return "Transparente";
    }
    return "?";
}

// Material PBR de metal/rugosidade (o mesmo modelo do glTF). Mapas nulos = valor constante do campo.
struct Material {
    glm::vec4 color{1.0f};                // multiplicado pelo albedo (espaço sRGB)
    std::shared_ptr<Texture2D> albedo;    // nulo => branco
    float metallic = 0.0f;                // 0 = não metal (plástico, madeira), 1 = metal
    float roughness = 0.5f;               // 0 = espelho, 1 = fosco
    float specular = 0.5f;                // reflexo dos não metais: 0.5 = 4% (o comum); 0 = nenhum
    std::shared_ptr<Texture2D> normalMap;            // espaço tangente, +Y para cima (OpenGL/glTF)
    float normalStrength = 1.0f;
    std::shared_ptr<Texture2D> metallicRoughnessMap; // como no glTF: G = rugosidade, B = metal (multiplicam os campos)
    std::shared_ptr<Texture2D> occlusionMap;         // R = oclusão ambiente (1 = sem oclusão)
    float occlusionStrength = 1.0f;
    glm::vec3 emissive{0.0f};                        // luz própria (sRGB), multiplicada pela intensidade
    float emissiveIntensity = 1.0f;
    std::shared_ptr<Texture2D> emissiveMap;
    BlendMode blend = BlendMode::Opaque;
    float alphaCutoff = 0.5f;  // Cutout: alfa abaixo disso some
    bool doubleSided = false;  // as duas faces aparecem (planos de folha, grade, vidro fino)
};

// Materiais antigos (Blinn-Phong, até a Fase 6) tinham "brilho" (expoente de 1 a 256) no lugar da rugosidade.
// Inversa da conversão que o importador glTF da Fase 11 fazia (brilho = 4 + 124 * (1 - rugosidade)²).
inline float RoughnessFromShininess(float shininess) {
    const float smooth = std::sqrt(std::clamp((shininess - 4.0f) / 124.0f, 0.0f, 1.0f));
    return 1.0f - smooth;
}

} // namespace motor
