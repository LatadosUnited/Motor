#include "motor/ui/UiRenderer.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/renderer/Texture.h"
#include "motor/scene/Scene.h"
#include "motor/ui/UiDocument.h"

#include <glm/common.hpp>

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

void UiCanvas::Rebuild(CachedElement& cached, const ElementKey& key, const FontData* font) {
    cached.key = key;
    cached.background.clear();
    cached.text.clear();
    cached.version = ++m_nextVersion;  // global: id reaproveitado nunca repete uma versão antiga
    ++m_stats.geometryRebuilds;

    if (key.hasBackground) {
        // Textura: linha 0 = base da imagem, então o topo do retângulo usa v = 1. Sem textura: bloco branco do
        // atlas da fonte padrão (mesmo lote do texto).
        glm::vec2 uvMin{0.0f, 1.0f}, uvMax{1.0f, 0.0f};
        if (!key.texture) {
            if (const FontData* white = DefaultFont()) uvMin = uvMax = white->WhiteUv();
        }
        UiBox b = key.box;
        const glm::vec4& c = key.background;
        if (!key.clipped || ClipQuad(b.min, b.max, uvMin, uvMax, key.clip)) {
            cached.background = {{{b.min.x, b.min.y}, {uvMin.x, uvMin.y}, c, 0.0f},
                                 {{b.max.x, b.min.y}, {uvMax.x, uvMin.y}, c, 0.0f},
                                 {{b.max.x, b.max.y}, {uvMax.x, uvMax.y}, c, 0.0f},
                                 {{b.min.x, b.max.y}, {uvMin.x, uvMax.y}, c, 0.0f}};
        }
    }
    if (font && !key.text.empty()) {
        LayoutText(*font, key.text, key.fontSize, key.align, key.textBox, m_quads, key.wrapWidth, key.vertical);
        if (key.clipped) {
            std::erase_if(m_quads, [&](TextQuad& q) { return !ClipQuad(q.min, q.max, q.uvMin, q.uvMax, key.clip); });
        }
        cached.text.reserve(m_quads.size() * 4);
        const glm::vec4& c = key.textColor;
        for (const TextQuad& q : m_quads) {
            cached.text.push_back({{q.min.x, q.min.y}, {q.uvMin.x, q.uvMin.y}, c, 1.0f});
            cached.text.push_back({{q.max.x, q.min.y}, {q.uvMax.x, q.uvMin.y}, c, 1.0f});
            cached.text.push_back({{q.max.x, q.max.y}, {q.uvMax.x, q.uvMax.y}, c, 1.0f});
            cached.text.push_back({{q.min.x, q.max.y}, {q.uvMin.x, q.uvMax.y}, c, 1.0f});
        }
    }
}

bool UiCanvas::Build(const Scene& scene, const AssetLibrary& assets, const glm::vec2& screenSize) {
    m_layout = ComputeUiLayout(scene, screenSize);
    const float scale = UiScale(screenSize);
    for (auto& [id, cached] : m_cache) cached.used = false;

    std::vector<Piece> pieces;
    pieces.reserve(m_layout.size());
    for (const UiNode& node : m_layout) {
        ElementKey key;
        key.box = node.box;
        key.textBox = node.content;
        key.clip = node.clip;
        key.clipped = node.clipped;
        if (const UiElement* element = node.element) {
            // Elemento de um documento: os mesmos campos, vindos da árvore viva.
            key.hasBackground = element->HasBackground();
            key.background = element->BackgroundColor();
            key.texture = element->texture.get();
            const FontData* font = nullptr;
            if (element->HasText()) {
                font = element->Font();  // a mesma que o layout usou para medir
                key.text = element->text;
                key.font = font;
                key.fontSize = element->fontSize * scale;
                key.textColor = element->textColor;
                key.align = element->align;
                key.vertical = element->verticalAlign;
                key.wrapWidth = element->wrap ? std::max(1.0f, node.content.Size().x) : 0.0f;
            }
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
        const FontData* defaultFont = DefaultFont();
        const Texture2D* white = defaultFont && m_atlasTexture ? m_atlasTexture(*defaultFont) : nullptr;
        for (const Piece& piece : m_pieces) {
            const CachedElement& cached = m_cache.at(piece.id);
            m_list.AddQuads(cached.key.texture ? cached.key.texture : white, cached.background);
            if (!cached.text.empty()) {
                const Texture2D* atlas = m_atlasTexture ? m_atlasTexture(*cached.key.font) : nullptr;
                m_list.AddQuads(atlas, cached.text);
            }
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
}

UiRenderer::~UiRenderer() = default;

void UiRenderer::Render(const Scene& scene, const AssetLibrary& assets, int width, int height, const Framebuffer* target) {
    if (width <= 0 || height <= 0) return;
    const bool changed = m_canvas.Build(scene, assets, {static_cast<float>(width), static_cast<float>(height)});
    m_renderer.Render(m_canvas.List(), width, height, target, changed);
}

} // namespace motor
