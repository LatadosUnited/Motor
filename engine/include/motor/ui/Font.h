#pragma once

// Fontes da UI: um .ttf/.otf vira um atlas SDF (campo de distância com sinal), feito uma vez por fonte. Cada
// texel guarda a distância até a borda do glifo, então o mesmo atlas desenha o texto nítido em qualquer
// tamanho (o shader recorta pela distância). Rasterização pelo stb_truetype que vem com o ImGui.
//
// FontData é só CPU (testável sem janela); a textura do atlas é criada pelo UiRenderer.

#include "motor/ui/UiComponents.h"
#include "motor/ui/UiLayout.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

struct FontGlyph {
    glm::vec2 offset{0.0f};  // do ponto da linha de base até o canto superior esquerdo do quadrado, em pixels
    glm::vec2 size{0.0f};    // pixels (zero para espaço)
    glm::vec2 uvMin{0.0f};   // no atlas (canto superior esquerdo)
    glm::vec2 uvMax{0.0f};
    float advance = 0.0f;    // quanto a caneta anda depois do glifo
};

class FontData {
public:
    // Tamanho em que o atlas é gerado e a margem da distância (pixels nesse tamanho). As medidas dos glifos
    // estão nesse tamanho; LayoutText escala para o tamanho pedido.
    static constexpr float kAtlasSize = 40.0f;
    static constexpr int kSdfPadding = 5;

    FontData();
    ~FontData();
    FontData(FontData&&) noexcept;
    FontData& operator=(FontData&&) noexcept;

    bool LoadFile(const std::filesystem::path& path, std::string* error = nullptr);
    bool LoadMemory(std::vector<uint8_t> bytes, std::string* error = nullptr);
    bool IsLoaded() const;

    // Glifo do caractere, ou o de '?' se a fonte (ou o atlas) não tiver. Atlas: ASCII e Latin-1 (acentos).
    const FontGlyph& Glyph(char32_t codepoint) const;
    bool HasGlyph(char32_t codepoint) const;
    float Kerning(char32_t left, char32_t right) const;  // ajuste entre os dois, no tamanho do atlas

    float Ascent() const { return m_ascent; }    // acima da linha de base (positivo)
    float Descent() const { return m_descent; }  // abaixo (negativo)
    float LineGap() const { return m_lineGap; }
    float LineHeight() const { return m_ascent - m_descent + m_lineGap; }

    // Atlas RGBA (RGB branco, alfa = distância; 0.5 = borda), linha 0 = topo. Tem um bloco branco opaco
    // para retângulos sólidos no mesmo lote do texto.
    int AtlasWidth() const { return m_atlasWidth; }
    int AtlasHeight() const { return m_atlasHeight; }
    const std::vector<uint8_t>& AtlasPixels() const { return m_atlas; }
    glm::vec2 WhiteUv() const { return m_whiteUv; }

private:
    struct Impl;  // stbtt_fontinfo e os bytes do arquivo (ficam vivos para o kerning)
    std::unique_ptr<Impl> m_impl;
    std::vector<char32_t> m_codepoints;  // ordenados, mesma ordem de m_glyphs
    std::vector<FontGlyph> m_glyphs;
    FontGlyph m_empty;
    float m_ascent = 0.0f;
    float m_descent = 0.0f;
    float m_lineGap = 0.0f;
    int m_atlasWidth = 0;
    int m_atlasHeight = 0;
    std::vector<uint8_t> m_atlas;
    glm::vec2 m_whiteUv{0.0f};
};

// Caminho da fonte padrão da engine (Roboto Medium, engine/assets/fonts).
std::filesystem::path DefaultFontPath();

// Fontes carregadas, compartilhadas por quem mede (layout) e quem desenha (UiCanvas): a mesma FontData para o mesmo
// arquivo. Falha fica guardada (nullptr, `error` só na primeira vez). Só na thread principal.
std::shared_ptr<const FontData> LoadUiFont(const std::filesystem::path& file, std::string* error = nullptr);
const FontData* DefaultUiFont();  // a padrão; nullptr só se nem ela abrir

// Próximo caractere de um texto UTF-8 (sequência inválida vira U+FFFD). Avança `text`.
char32_t NextCodepoint(std::string_view& text);

struct TextQuad {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};
    glm::vec2 uvMin{0.0f};
    glm::vec2 uvMax{0.0f};
};

// Largura e altura do texto em pixels, com `pixelSize` de altura de fonte. Linhas separadas por '\n'; com
// `wrapWidth` > 0, a linha também quebra nos espaços para caber nessa largura (palavra maior que ela fica sozinha
// na linha, passando da largura).
glm::vec2 MeasureText(const FontData& font, std::string_view text, float pixelSize, float wrapWidth = 0.0f);

// Quadrados dos glifos do texto dentro de `box`: cada linha alinhada na horizontal por `align` e o bloco na
// vertical por `vertical` (centrado por padrão). Quebra como MeasureText. O texto pode passar da caixa (quem
// corta é o recorte do UiCanvas).
void LayoutText(const FontData& font, std::string_view text, float pixelSize, UiTextAlign align, const UiBox& box,
                std::vector<TextQuad>& out, float wrapWidth = 0.0f, UiVerticalAlign vertical = UiVerticalAlign::Middle);

} // namespace motor
