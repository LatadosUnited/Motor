#include "motor/ui/UiRenderer.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/renderer/Texture.h"
#include "motor/scene/Scene.h"

#include <algorithm>

namespace motor {

// --- UiCanvas -------------------------------------------------------------------------

std::shared_ptr<FontData> UiCanvas::LoadFont(const std::string& key, const std::filesystem::path& file) {
    if (const auto it = m_fonts.find(key); it != m_fonts.end()) return it->second;
    auto font = std::make_shared<FontData>();
    std::string error;
    if (!font->LoadFile(file, &error)) {
        log::Warn("UI: fonte {}: {}", key, error);
        font = nullptr;  // fica no cache como falha: o aviso sai uma vez só
    }
    m_fonts[key] = font;
    return font;
}

const FontData* UiCanvas::DefaultFont() {
    const std::filesystem::path file = DefaultFontPath();
    return LoadFont(ToUtf8(file), file).get();
}

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
        const UiBox& b = key.box;
        const glm::vec4& c = key.background;
        cached.background = {{{b.min.x, b.min.y}, {uvMin.x, uvMin.y}, c, 0.0f},
                             {{b.max.x, b.min.y}, {uvMax.x, uvMin.y}, c, 0.0f},
                             {{b.max.x, b.max.y}, {uvMax.x, uvMax.y}, c, 0.0f},
                             {{b.min.x, b.max.y}, {uvMin.x, uvMax.y}, c, 0.0f}};
    }
    if (font && !key.text.empty()) {
        LayoutText(*font, key.text, key.fontSize, key.align, key.box, m_quads);
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
        const Entity* e = scene.Find(node.entity);
        if (!e) continue;
        ElementKey key;
        key.box = node.box;
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

        CachedElement& cached = m_cache[node.entity];
        if (cached.version == 0 || !(cached.key == key)) Rebuild(cached, key, font);
        cached.used = true;
        pieces.push_back({node.entity, cached.version});
    }
    std::erase_if(m_cache, [](const auto& item) { return !item.second.used; });

    const bool changed = pieces != m_pieces;
    if (changed) {
        m_pieces = std::move(pieces);
        m_list.Clear();
        const FontData* defaultFont = DefaultFont();
        const Texture2D* white = defaultFont && m_atlasTexture ? m_atlasTexture(*defaultFont) : nullptr;
        for (const Piece& piece : m_pieces) {
            const CachedElement& cached = m_cache.at(piece.entity);
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
