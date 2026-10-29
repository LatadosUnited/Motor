#include "motor/ui/UiRenderer.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/renderer/Texture.h"
#include "motor/scene/Scene.h"
#include "motor/ui/UiDocument.h"

#include "UiFlexLayout.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>

namespace motor {

// --- UiCanvas -------------------------------------------------------------------------

std::shared_ptr<const FontData> UiCanvas::LoadFont(const std::string& key, const std::filesystem::path& file) {
    if (const auto it = m_fonts.find(key); it != m_fonts.end()) return it->second;
    std::string error;
    auto font = LoadUiFont(file, &error);  // a mesma dos documentos, que o layout usa para medir
    if (!font) log::Warn("UI: fonte {}: {}", key, error.empty() ? std::string("não abriu") : error);
    m_fonts[key] = font;  // falha fica no cache: o aviso sai uma vez só
    return font;
}

const FontData* UiCanvas::DefaultFont() { return DefaultUiFont(); }

namespace {
// Corta o quadrado no retângulo, acertando as coordenadas de textura na mesma proporção. false se sobrou nada.
bool ClipQuad(glm::vec2& min, glm::vec2& max, glm::vec2& uvMin, glm::vec2& uvMax, const UiBox& clip) {
    const glm::vec2 cmin = glm::max(min, clip.min), cmax = glm::min(max, clip.max);
    if (cmin.x >= cmax.x || cmin.y >= cmax.y) return false;
    const glm::vec2 size = glm::max(max - min, glm::vec2(1e-6f));
    const glm::vec2 du = uvMax - uvMin;
    const glm::vec2 a = uvMin + du * ((cmin - min) / size);
    const glm::vec2 b = uvMin + du * ((cmax - min) / size);
    min = cmin;
    max = cmax;
    uvMin = a;
    uvMax = b;
    return true;
}
} // namespace

const FontData* UiCanvas::FontFor(const UiFont& font, const AssetLibrary& assets) {
    if (font.IsDefault()) return DefaultFont();
    std::filesystem::path file;
    if (!font.guid.empty()) file = assets.Database().PathFor(font.guid);
    if (file.empty() && !font.path.empty()) file = assets.FileForKey(font.path);
    if (file.empty()) {
        const std::string key = "?" + font.guid + "|" + font.path;  // não achado: avisa uma vez
        if (!m_fonts.contains(key)) {
            log::Warn("UI: fonte não encontrada ({}); usando a padrão", font.path.empty() ? font.guid : font.path);
            m_fonts[key] = nullptr;
        }
        return DefaultFont();
    }
    if (const FontData* loaded = LoadFont(ToUtf8(file), file).get()) return loaded;
    return DefaultFont();
}

namespace {
// Quadrado (recortado, se houver recorte) na lista de vértices.
void PushQuad(std::vector<Vertex2D>& out, glm::vec2 min, glm::vec2 max, glm::vec2 uvMin, glm::vec2 uvMax,
              const glm::vec4& c, float sdf, const UiBox* clip) {
    if (clip && !ClipQuad(min, max, uvMin, uvMax, *clip)) return;
    out.push_back({{min.x, min.y}, {uvMin.x, uvMin.y}, c, sdf});
    out.push_back({{max.x, min.y}, {uvMax.x, uvMin.y}, c, sdf});
    out.push_back({{max.x, max.y}, {uvMax.x, uvMax.y}, c, sdf});
    out.push_back({{min.x, max.y}, {uvMin.x, uvMax.y}, c, sdf});
}

// Os 8 lados do contorno do texto (o texto desenhado deslocado em volta, por baixo).
constexpr float kDiagonal = 0.70710678f;
constexpr glm::vec2 kOutlineDirections[] = {{1, 0},          {-1, 0},          {0, 1},           {0, -1},
                                            {kDiagonal, kDiagonal}, {-kDiagonal, kDiagonal}, {kDiagonal, -kDiagonal},
                                            {-kDiagonal, -kDiagonal}};

// Escala de desenho acumulada (scale do elemento em volta do centro dele, dentro da dos pais) e opacidade.
struct NodeLook {
    float k = 1.0f;
    glm::vec2 offset{0.0f};
    float opacity = 1.0f;

    UiBox Apply(const UiBox& b) const { return {b.min * k + offset, b.max * k + offset}; }
};
} // namespace

std::vector<uint8_t> MakeUiCirclePixels(int size) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * static_cast<size_t>(size) * 4, 255);
    const float radius = static_cast<float>(size) * 0.5f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const glm::vec2 p{static_cast<float>(x) + 0.5f - radius, static_cast<float>(y) + 0.5f - radius};
            const float coverage = std::clamp(radius - glm::length(p) + 0.5f, 0.0f, 1.0f);  // borda de 1 pixel
            pixels[(static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 4 + 3] =
                static_cast<uint8_t>(coverage * 255.0f + 0.5f);
        }
    }
    return pixels;
}

void AppendUiNineSlice(std::vector<Vertex2D>& out, const UiBox& box, const UiEdges& border, glm::vec2 uvMin,
                       glm::vec2 uvMax, const UiEdges& uvBorder, const glm::vec4& color, const UiBox* clip) {
    const glm::vec2 size = glm::max(box.Size(), glm::vec2(0.0f));
    float left = std::max(0.0f, border.left), right = std::max(0.0f, border.right);
    float top = std::max(0.0f, border.top), bottom = std::max(0.0f, border.bottom);
    if (left + right > size.x && left + right > 0.0f) {  // caixa menor que os cantos: encolhem juntos
        const float f = size.x / (left + right);
        left *= f;
        right *= f;
    }
    if (top + bottom > size.y && top + bottom > 0.0f) {
        const float f = size.y / (top + bottom);
        top *= f;
        bottom *= f;
    }
    const glm::vec2 du = uvMax - uvMin;
    const float xs[4] = {box.min.x, box.min.x + left, box.max.x - right, box.max.x};
    const float ys[4] = {box.min.y, box.min.y + top, box.max.y - bottom, box.max.y};
    const float us[4] = {uvMin.x, uvMin.x + du.x * uvBorder.left, uvMax.x - du.x * uvBorder.right, uvMax.x};
    const float vs[4] = {uvMin.y, uvMin.y + du.y * uvBorder.top, uvMax.y - du.y * uvBorder.bottom, uvMax.y};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (xs[col + 1] <= xs[col] || ys[row + 1] <= ys[row]) continue;
            PushQuad(out, {xs[col], ys[row]}, {xs[col + 1], ys[row + 1]}, {us[col], vs[row]}, {us[col + 1], vs[row + 1]},
                     color, 0.0f, clip);
        }
    }
}

void UiCanvas::Rebuild(CachedElement& cached, const ElementKey& key, const FontData* font) {
    cached.key = key;
    cached.segments.clear();
    cached.version = ++m_nextVersion;  // global: id reaproveitado nunca repete uma versão antiga
    ++m_stats.geometryRebuilds;

    const UiBox* clip = key.clipped ? &key.clip : nullptr;
    // Pedaços seguidos com a mesma textura ficam num segmento só (uma troca de textura a menos no lote).
    auto segment = [&](const Texture2D* texture) -> std::vector<Vertex2D>& {
        if (cached.segments.empty() || cached.segments.back().texture != texture) cached.segments.push_back({texture, {}});
        return cached.segments.back().vertices;
    };
    auto fade = [&](glm::vec4 c) {
        c.a *= key.opacity;
        return c;
    };

    // Sem textura e sem cantos: bloco branco do atlas da fonte padrão (mesmo lote do texto).
    const FontData* defaultFont = DefaultFont();
    const glm::vec2 whiteUv = defaultFont ? defaultFont->WhiteUv() : glm::vec2(0.0f);
    const Texture2D* white = defaultFont && m_atlasTexture ? m_atlasTexture(*defaultFont) : nullptr;
    // Pedaço de um controle: cantos arredondados (o círculo em 9 pedaços) ou o retângulo branco.
    auto partShape = [&](const UiBox& b, float r, const glm::vec4& c) {
        const float half = std::max(0.0f, std::min(b.Size().x, b.Size().y) * 0.5f);
        r = std::clamp(r, 0.0f, half);
        if (r > 0.0f) {
            const Texture2D* circle = m_shapeTexture ? m_shapeTexture() : nullptr;
            AppendUiNineSlice(segment(circle), b, UiEdges::All(r), {0.0f, 0.0f}, {1.0f, 1.0f}, UiEdges::All(0.5f), c, clip);
        } else {
            PushQuad(segment(white), b.min, b.max, whiteUv, whiteUv, c, 0.0f, clip);
        }
    };

    if (key.hasBackground) {
        const glm::vec2 size = key.box.Size();
        const float half = std::max(0.0f, std::min(size.x, size.y) * 0.5f);
        const float radius = std::clamp(key.radius, 0.0f, half);
        const float border = std::clamp(key.borderWidth, 0.0f, half);
        const bool rounded = radius > 0.0f;
        const Texture2D* circle = rounded && m_shapeTexture ? m_shapeTexture() : nullptr;
        auto shape = [&](const UiBox& b, float r, const glm::vec4& c) {
            if (rounded) {  // círculo em 9 pedaços: quartos nos cantos, o centro branco no resto
                AppendUiNineSlice(segment(circle), b, UiEdges::All(r), {0.0f, 0.0f}, {1.0f, 1.0f}, UiEdges::All(0.5f), c, clip);
            } else {
                PushQuad(segment(white), b.min, b.max, whiteUv, whiteUv, c, 0.0f, clip);
            }
        };
        UiBox inner = key.box;
        if (border > 0.0f && key.borderColor.a > 0.0f) {
            shape(key.box, radius, fade(key.borderColor));  // a borda é a forma inteira, por baixo do fundo
            inner = {key.box.min + glm::vec2(border), key.box.max - glm::vec2(border)};
        }
        const float innerRadius = std::max(0.0f, radius - border);
        if (key.texture) {
            // Textura: linha 0 = base da imagem, então o topo do retângulo usa v = 1.
            const glm::vec2 uvMin{0.0f, 1.0f}, uvMax{1.0f, 0.0f};
            std::vector<Vertex2D>& out = segment(key.texture);
            if (!key.slice.IsZero() && key.textureSize.x > 0.0f && key.textureSize.y > 0.0f) {
                const UiEdges& s = key.slice;
                const UiEdges uvBorder{s.top / key.textureSize.y, s.right / key.textureSize.x,
                                       s.bottom / key.textureSize.y, s.left / key.textureSize.x};
                const UiEdges screen{s.top * key.slicePixel, s.right * key.slicePixel, s.bottom * key.slicePixel,
                                     s.left * key.slicePixel};
                AppendUiNineSlice(out, inner, screen, uvMin, uvMax, uvBorder, fade(key.background), clip);
            } else {
                PushQuad(out, inner.min, inner.max, uvMin, uvMax, fade(key.background), 0.0f, clip);
            }
        } else {
            shape(inner, innerRadius, fade(key.background));
        }
    }
    for (const ElementPart& part : key.parts) {
        if (part.box.max.x > part.box.min.x && part.box.max.y > part.box.min.y && part.color.a > 0.0f) {
            partShape(part.box, part.radius, fade(part.color));
        }
    }
    if (font && !key.text.empty()) {
        LayoutText(*font, key.text, key.fontSize, key.align, key.textBox, m_quads, key.wrapWidth, key.vertical);
        const Texture2D* atlas = m_atlasTexture ? m_atlasTexture(*font) : nullptr;
        auto emit = [&](glm::vec2 offset, const glm::vec4& color) {
            const glm::vec4 c = fade(color);
            std::vector<Vertex2D>& out = segment(atlas);
            out.reserve(out.size() + m_quads.size() * 4);
            for (const TextQuad& q : m_quads) PushQuad(out, q.min + offset, q.max + offset, q.uvMin, q.uvMax, c, 1.0f, clip);
        };
        // Por baixo do texto: a sombra, depois o contorno (o texto deslocado em 8 direções).
        if (key.shadowColor.a > 0.0f && key.shadowOffset != glm::vec2(0.0f)) emit(key.shadowOffset, key.shadowColor);
        if (key.outlineWidth > 0.0f && key.outlineColor.a > 0.0f) {
            for (const glm::vec2& dir : kOutlineDirections) emit(dir * key.outlineWidth, key.outlineColor);
        }
        emit({0.0f, 0.0f}, key.textColor);
    }
}

void UiCanvas::AddControlParts(const UiElement& e, ElementKey& key, float pixel, const FontData* font) {
    // `pixel`: pixels da tela por pixel da referência (escala da tela e do desenho). Medidas a partir do conteúdo.
    const UiBox content = key.textBox;
    const glm::vec2 size = content.Size();
    const float radius = std::max(0.0f, e.radius * pixel);
    // Borda (o :focus) do slider e da caixa de seleção: em volta do puxador e da caixa, não do elemento (o slider não
    // tem fundo e o da caixa costuma ser transparente: a borda viraria um bloco cheio por baixo dele).
    const float ring = key.borderColor.a > 0.0f ? key.borderWidth : 0.0f;
    auto withRing = [&](const UiBox& b, const glm::vec4& color, float r) {
        if (ring > 0.0f) key.parts.push_back({{b.min - glm::vec2(ring), b.max + glm::vec2(ring)}, key.borderColor, r + ring});
        key.parts.push_back({b, color, r});
    };
    switch (e.type) {
        case UiElementType::ProgressBar: {
            const float inner = std::max(0.0f, radius - std::max(e.padding.left, e.padding.top) * pixel);
            key.parts.push_back({{content.min, {content.min.x + size.x * e.Fraction(), content.max.y}}, e.fillColor, inner});
            break;
        }
        case UiElementType::Slider: {
            float x0 = 0.0f, x1 = 0.0f;
            UiSliderTrack(content, x0, x1);
            const float cy = content.Center().y;
            const float band = std::max(4.0f * pixel, size.y * 0.3f) * 0.5f;
            const float x = x0 + (x1 - x0) * e.Fraction();
            const float thumb = UiSliderThumbWidth(content) * 0.5f;
            key.parts.push_back({{{x0, cy - band}, {x1, cy + band}}, e.color, radius});
            key.parts.push_back({{{x0, cy - band}, {x, cy + band}}, e.fillColor, radius});
            withRing({{x - thumb, content.min.y}, {x + thumb, content.max.y}}, e.StateColor(), radius);
            break;
        }
        case UiElementType::Toggle: {
            const float box = UiToggleBoxSize(e, pixel);
            const glm::vec2 min{content.min.x, content.Center().y - box * 0.5f};
            withRing({min, min + glm::vec2(box)}, e.StateColor(), radius);
            key.borderWidth = 0.0f;
            if (e.checked) {
                const float inset = box * 0.22f;
                key.parts.push_back({{min + glm::vec2(inset), min + glm::vec2(box - inset)}, e.fillColor,
                                     std::max(0.0f, radius - inset)});
            }
            key.textBox.min.x = std::min(content.max.x, content.min.x + box * 1.3f);
            break;
        }
        case UiElementType::TextField: {
            if (e.text.empty()) key.textColor.a *= 0.5f;  // o placeholder, apagado
            float full = 0.0f;
            if (font && !key.text.empty()) full = MeasureText(*font, key.text, key.fontSize).x;
            if (full > size.x) key.align = UiTextAlign::Right;  // texto maior que a caixa: mostra o fim (onde digita)
            if (e.focused && font) {
                const size_t caret = std::min(e.caret, e.text.size());
                const float before =
                    caret > 0 ? MeasureText(*font, std::string_view(e.text).substr(0, caret), key.fontSize).x : 0.0f;
                const float shown = e.text.empty() ? 0.0f : full;
                float start = content.min.x;
                if (key.align == UiTextAlign::Right) start = content.max.x - shown;
                else if (key.align == UiTextAlign::Center) start = content.Center().x - shown * 0.5f;
                const float x = std::clamp(start + before, content.min.x, content.max.x);
                const float h = key.fontSize * 1.1f;
                const float w = std::max(1.0f, 2.0f * pixel);
                key.parts.push_back(
                    {{{x, content.Center().y - h * 0.5f}, {x + w, content.Center().y + h * 0.5f}}, e.fillColor, 0.0f});
            }
            break;
        }
        default: break;
    }
}

bool UiCanvas::Build(const Scene& scene, const AssetLibrary& assets, const glm::vec2& screenSize) {
    m_layout = ComputeUiLayout(scene, screenSize);
    const float scale = UiScale(screenSize);
    for (auto& [id, cached] : m_cache) cached.used = false;

    std::vector<Piece> pieces;
    pieces.reserve(m_layout.size());
    std::vector<NodeLook> looks(m_layout.size());
    for (size_t i = 0; i < m_layout.size(); ++i) {
        const UiNode& node = m_layout[i];
        ElementKey key;
        key.box = node.box;
        key.textBox = node.content;
        key.clip = node.clip;
        key.clipped = node.clipped;
        if (const UiElement* element = node.element) {
            // Elemento de um documento: os mesmos campos, vindos da árvore viva, com os valores da transição.
            const UiVisual shown = element->ShownVisual();
            const NodeLook parent = node.parent >= 0 ? looks[static_cast<size_t>(node.parent)] : NodeLook{};
            const float s = std::max(0.0f, shown.scale);
            NodeLook& look = looks[i];
            look.k = s * parent.k;
            look.offset = node.box.Center() * (1.0f - s) * parent.k + parent.offset;
            look.opacity = std::clamp(shown.opacity, 0.0f, 1.0f) * parent.opacity;
            const float k = look.k;  // pixels da tela por pixel do layout
            key.box = look.Apply(node.box);
            key.textBox = look.Apply(node.content);
            if (node.clipped) key.clip = parent.Apply(node.clip);
            key.opacity = look.opacity;
            key.hasBackground = element->HasBackground();
            key.background = shown.background;
            key.texture = element->texture.get();
            if (key.texture) key.textureSize = {static_cast<float>(key.texture->Width()), static_cast<float>(key.texture->Height())};
            key.radius = element->radius * scale * k;
            key.borderWidth = element->borderWidth * scale * k;
            key.borderColor = shown.borderColor;
            key.slice = element->slice;
            key.slicePixel = scale * k;
            const FontData* font = nullptr;
            if (element->HasText()) {
                font = element->Font();  // a mesma que o layout usou para medir
                key.text = std::string(element->DisplayText());
                key.font = font;
                key.fontSize = element->fontSize * scale * k;
                key.textColor = shown.textColor;
                key.align = element->align;
                key.vertical = element->verticalAlign;
                key.wrapWidth = element->wrap ? std::max(1.0f, node.content.Size().x * k) : 0.0f;
                key.outlineWidth = element->textOutlineWidth * scale * k;
                key.outlineColor = element->textOutlineColor;
                key.shadowOffset = element->textShadowOffset * scale * k;
                key.shadowColor = element->textShadowColor;
            }
            AddControlParts(*element, key, scale * k, font);
            const PieceId id{node.entity, element};
            CachedElement& cached = m_cache[id];
            if (cached.version == 0 || !(cached.key == key)) Rebuild(cached, key, font);
            cached.used = true;
            pieces.push_back({id, cached.version});
            continue;
        }
        const Entity* e = scene.Find(node.entity);
        if (!e) continue;
        const UiImage* image = e->Get<UiImage>();
        if (const UiButton* button = e->Get<UiButton>()) {
            key.hasBackground = true;
            key.background = button->CurrentColor() * (image ? image->color : glm::vec4(1.0f));
            key.texture = image ? image->texture.get() : nullptr;
        } else if (image) {
            key.hasBackground = true;
            key.background = image->color;
            key.texture = image->texture.get();
        }
        const FontData* font = nullptr;
        if (const UiText* text = e->Get<UiText>()) {
            font = FontFor(text->font, assets);
            key.text = text->text;
            key.font = font;
            key.fontSize = text->fontSize * scale;
            key.textColor = text->color;
            key.align = text->align;
        }

        CachedElement& cached = m_cache[PieceId{node.entity, nullptr}];
        if (cached.version == 0 || !(cached.key == key)) Rebuild(cached, key, font);
        cached.used = true;
        pieces.push_back({PieceId{node.entity, nullptr}, cached.version});
    }
    std::erase_if(m_cache, [](const auto& item) { return !item.second.used; });

    const bool changed = pieces != m_pieces;
    if (changed) {
        m_pieces = std::move(pieces);
        m_list.Clear();
        for (const Piece& piece : m_pieces) {
            for (const Segment& segment : m_cache.at(piece.id).segments) m_list.AddQuads(segment.texture, segment.vertices);
        }
        ++m_stats.listRebuilds;
    }
    m_stats.quads = m_list.QuadCount();
    m_stats.batches = m_list.Batches().size();
    return changed;
}

// --- UiRenderer -----------------------------------------------------------------------

UiRenderer::UiRenderer() {
    m_canvas.SetAtlasTextureSource([this](const FontData& font) -> const Texture2D* {
        auto& texture = m_atlases[&font];
        if (!texture) {
            texture = std::make_unique<Texture2D>(font.AtlasWidth(), font.AtlasHeight(), font.AtlasPixels().data(),
                                                  TextureFilter::Linear, TextureColorSpace::Linear);
        }
        return texture.get();
    });
    m_canvas.SetShapeTextureSource([this]() -> const Texture2D* {
        if (!m_circle) {
            const std::vector<uint8_t> pixels = MakeUiCirclePixels(kUiCircleTextureSize);
            m_circle = std::make_unique<Texture2D>(kUiCircleTextureSize, kUiCircleTextureSize, pixels.data(),
                                                   TextureFilter::Linear, TextureColorSpace::Linear);
        }
        return m_circle.get();
    });
}

UiRenderer::~UiRenderer() = default;

void UiRenderer::Render(const Scene& scene, const AssetLibrary& assets, int width, int height, const Framebuffer* target) {
    if (width <= 0 || height <= 0) return;
    const bool changed = m_canvas.Build(scene, assets, {static_cast<float>(width), static_cast<float>(height)});
    m_renderer.Render(m_canvas.List(), width, height, target, changed);
}

} // namespace motor
