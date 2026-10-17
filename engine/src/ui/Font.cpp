#include "motor/ui/Font.h"

#include "motor/core/EngineAssets.h"
#include "motor/core/Paths.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <utility>

// stb_truetype 1.26, a cópia que vem com o ImGui (imstb_truetype.h). STBTT_STATIC: as funções ficam só neste
// arquivo, sem colidir com a cópia que o próprio ImGui compila no imgui_draw.cpp. Avisos dele não são nossos.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <imstb_truetype.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace motor {

namespace fs = std::filesystem;

struct FontData::Impl {
    std::vector<uint8_t> bytes;
    stbtt_fontinfo info{};
    float scale = 1.0f;  // unidades da fonte -> pixels no tamanho do atlas
};

FontData::FontData() = default;
FontData::~FontData() = default;
FontData::FontData(FontData&&) noexcept = default;
FontData& FontData::operator=(FontData&&) noexcept = default;

fs::path DefaultFontPath() {
    return EngineAssetsDir() / "fonts" / "Roboto-Medium.ttf";
}

char32_t NextCodepoint(std::string_view& text) {
    if (text.empty()) return 0;
    const auto c = static_cast<unsigned char>(text[0]);
    int length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
    if (length == 0 || static_cast<size_t>(length) > text.size()) {
        text.remove_prefix(1);
        return 0xFFFD;
    }
    char32_t cp = length == 1 ? c : (c & (0x7F >> length));
    for (int i = 1; i < length; ++i) {
        const auto next = static_cast<unsigned char>(text[static_cast<size_t>(i)]);
        if ((next & 0xC0) != 0x80) {
            text.remove_prefix(static_cast<size_t>(i));
            return 0xFFFD;
        }
        cp = (cp << 6) | (next & 0x3F);
    }
    text.remove_prefix(static_cast<size_t>(length));
    return cp;
}

bool FontData::LoadFile(const fs::path& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "não foi possível abrir " + ToUtf8(path);
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return LoadMemory(std::move(bytes), error);
}

bool FontData::IsLoaded() const {
    return m_impl != nullptr;
}

bool FontData::LoadMemory(std::vector<uint8_t> bytes, std::string* error) {
    auto impl = std::make_unique<Impl>();
    impl->bytes = std::move(bytes);
    const int offset = impl->bytes.empty() ? -1 : stbtt_GetFontOffsetForIndex(impl->bytes.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&impl->info, impl->bytes.data(), offset)) {
        if (error) *error = "arquivo de fonte inválido (esperado TrueType/OpenType)";
        return false;
    }
    impl->scale = stbtt_ScaleForPixelHeight(&impl->info, kAtlasSize);

    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&impl->info, &ascent, &descent, &lineGap);
    m_ascent = static_cast<float>(ascent) * impl->scale;
    m_descent = static_cast<float>(descent) * impl->scale;
    m_lineGap = static_cast<float>(lineGap) * impl->scale;

    // Caracteres do atlas: ASCII visível, Latin-1 (acentos do português) e algumas pontuações comuns.
    std::vector<char32_t> wanted;
    for (char32_t c = 32; c < 127; ++c) wanted.push_back(c);
    for (char32_t c = 160; c < 256; ++c) wanted.push_back(c);
    for (char32_t c : {U'–', U'—', U'‘', U'’', U'“', U'”', U'•', U'…', U'€', U'™'}) wanted.push_back(c);

    struct Bitmap {
        char32_t codepoint;
        FontGlyph glyph;
        int width = 0, height = 0;
        unsigned char* pixels = nullptr;
    };
    std::vector<Bitmap> bitmaps;
    const float pixelDistScale = 128.0f / static_cast<float>(kSdfPadding);  // 0.5 = borda, ±padding px até 0 e 1
    for (char32_t c : wanted) {
        const int index = stbtt_FindGlyphIndex(&impl->info, static_cast<int>(c));
        if (index == 0 && c != U' ') continue;  // a fonte não tem
        Bitmap b{c, {}, 0, 0, nullptr};
        int advance = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&impl->info, index, &advance, &lsb);
        b.glyph.advance = static_cast<float>(advance) * impl->scale;
        int xoff = 0, yoff = 0;
        b.pixels = stbtt_GetGlyphSDF(&impl->info, impl->scale, index, kSdfPadding, 128, pixelDistScale, &b.width,
                                     &b.height, &xoff, &yoff);
        b.glyph.offset = {static_cast<float>(xoff), static_cast<float>(yoff)};
        b.glyph.size = {static_cast<float>(b.width), static_cast<float>(b.height)};
        bitmaps.push_back(b);
    }

    // Prateleiras: mais altos primeiro, da esquerda para a direita, 1 pixel de folga. O bloco branco vem antes.
    constexpr int kWidth = 512;
    constexpr int kWhite = 4;
    std::vector<size_t> order(bitmaps.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return bitmaps[a].height > bitmaps[b].height; });
    struct Place { int x, y; };
    std::vector<Place> places(bitmaps.size(), {0, 0});
    int x = kWhite + 1, y = 0, shelf = kWhite;
    for (size_t i : order) {
        const Bitmap& b = bitmaps[i];
        if (x + b.width + 1 > kWidth) {
            x = 0;
            y += shelf + 1;
            shelf = 0;
        }
        places[i] = {x, y};
        x += b.width + 1;
        shelf = std::max(shelf, b.height);
    }
    int height = 1;
    while (height < y + shelf + 1) height *= 2;

    m_atlasWidth = kWidth;
    m_atlasHeight = height;
    m_atlas.assign(static_cast<size_t>(kWidth) * static_cast<size_t>(height) * 4, 0);
    for (size_t i = 0; i < m_atlas.size(); i += 4) m_atlas[i] = m_atlas[i + 1] = m_atlas[i + 2] = 255;
    auto setAlpha = [&](int px, int py, uint8_t a) {
        m_atlas[(static_cast<size_t>(py) * kWidth + static_cast<size_t>(px)) * 4 + 3] = a;
    };
    for (int py = 0; py < kWhite; ++py) {
        for (int px = 0; px < kWhite; ++px) setAlpha(px, py, 255);
    }
    m_whiteUv = {kWhite * 0.5f / kWidth, kWhite * 0.5f / static_cast<float>(height)};

    const glm::vec2 atlasSize{static_cast<float>(kWidth), static_cast<float>(height)};
    m_codepoints.clear();
    m_glyphs.clear();
    std::vector<size_t> byCodepoint(bitmaps.size());
    for (size_t i = 0; i < byCodepoint.size(); ++i) byCodepoint[i] = i;
    std::sort(byCodepoint.begin(), byCodepoint.end(),
              [&](size_t a, size_t b) { return bitmaps[a].codepoint < bitmaps[b].codepoint; });
    for (size_t i : byCodepoint) {
        Bitmap& b = bitmaps[i];
        for (int py = 0; py < b.height; ++py) {
            for (int px = 0; px < b.width; ++px) {
                setAlpha(places[i].x + px, places[i].y + py, b.pixels[py * b.width + px]);
            }
        }
        const glm::vec2 corner{static_cast<float>(places[i].x), static_cast<float>(places[i].y)};
        b.glyph.uvMin = corner / atlasSize;
        b.glyph.uvMax = (corner + b.glyph.size) / atlasSize;
        if (b.pixels) stbtt_FreeSDF(b.pixels, nullptr);
        m_codepoints.push_back(b.codepoint);
        m_glyphs.push_back(b.glyph);
    }
    m_impl = std::move(impl);
    return true;
}

bool FontData::HasGlyph(char32_t codepoint) const {
    return std::binary_search(m_codepoints.begin(), m_codepoints.end(), codepoint);
}

const FontGlyph& FontData::Glyph(char32_t codepoint) const {
    for (char32_t c : {codepoint, U'?'}) {
        const auto it = std::lower_bound(m_codepoints.begin(), m_codepoints.end(), c);
        if (it != m_codepoints.end() && *it == c) return m_glyphs[static_cast<size_t>(it - m_codepoints.begin())];
    }
    return m_empty;
}

float FontData::Kerning(char32_t left, char32_t right) const {
    if (!m_impl) return 0.0f;
    return static_cast<float>(stbtt_GetCodepointKernAdvance(&m_impl->info, static_cast<int>(left), static_cast<int>(right))) *
           m_impl->scale;
}

namespace {
// Chama fn(linha) para cada linha do texto ('\n'; '\r' é ignorado no fim).
template <class Fn>
void ForEachLine(std::string_view text, Fn fn) {
    while (true) {
        const size_t eol = text.find('\n');
        std::string_view line = text.substr(0, eol);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        fn(line);
        if (eol == std::string_view::npos) break;
        text.remove_prefix(eol + 1);
    }
}

float LineWidth(const FontData& font, std::string_view line, float k) {
    float width = 0.0f;
    char32_t previous = 0;
    while (!line.empty()) {
        const char32_t c = NextCodepoint(line);
        if (previous) width += font.Kerning(previous, c) * k;
        width += font.Glyph(c).advance * k;
        previous = c;
    }
    return width;
}

int LineCount(std::string_view text) {
    return static_cast<int>(std::count(text.begin(), text.end(), '\n')) + 1;
}
} // namespace

glm::vec2 MeasureText(const FontData& font, std::string_view text, float pixelSize) {
    const float k = pixelSize / FontData::kAtlasSize;
    float width = 0.0f;
    ForEachLine(text, [&](std::string_view line) { width = std::max(width, LineWidth(font, line, k)); });
    const int lines = LineCount(text);
    const float height = (font.Ascent() - font.Descent()) * k + static_cast<float>(lines - 1) * font.LineHeight() * k;
    return {width, height};
}

void LayoutText(const FontData& font, std::string_view text, float pixelSize, UiTextAlign align, const UiBox& box,
                std::vector<TextQuad>& out) {
    out.clear();
    if (!font.IsLoaded() || pixelSize <= 0.0f) return;
    const float k = pixelSize / FontData::kAtlasSize;
    const float blockHeight = MeasureText(font, text, pixelSize).y;
    float baseline = box.Center().y - blockHeight * 0.5f + font.Ascent() * k;

    ForEachLine(text, [&](std::string_view line) {
        const float width = LineWidth(font, line, k);
        float pen = box.min.x;
        if (align == UiTextAlign::Center) pen = box.Center().x - width * 0.5f;
        if (align == UiTextAlign::Right) pen = box.max.x - width;
        char32_t previous = 0;
        while (!line.empty()) {
            const char32_t c = NextCodepoint(line);
            if (previous) pen += font.Kerning(previous, c) * k;
            const FontGlyph& g = font.Glyph(c);
            if (g.size.x > 0.0f && g.size.y > 0.0f) {
                const glm::vec2 min{pen + g.offset.x * k, baseline + g.offset.y * k};
                out.push_back({min, min + g.size * k, g.uvMin, g.uvMax});
            }
            pen += g.advance * k;
            previous = c;
        }
        baseline += font.LineHeight() * k;
    });
}

} // namespace motor
