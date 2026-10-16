#pragma once

// Desenho da UI do jogo por cima da cena (depois de Scene::Render), independente do ImGui do editor.
//
//   UiCanvas    CPU: layout, fontes e a lista de desenho (DrawList2D). Testável sem janela.
//   UiRenderer  GPU: UiCanvas + Renderer2D + as texturas dos atlas das fontes.
//
// Desempenho: todo o texto e as imagens vão num VBO dinâmico só, em uma chamada de desenho por troca de
// textura (retângulos sólidos usam o bloco branco do atlas da fonte e entram no mesmo lote do texto). A
// geometria de cada elemento fica em cache e só é refeita quando o texto, o tamanho, a fonte, a cor ou o
// retângulo mudam; se nada mudou no frame, nada é enviado para a GPU.

#include "motor/renderer/Renderer2D.h"
#include "motor/ui/Font.h"
#include "motor/ui/UiLayout.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace motor {

class AssetLibrary;
class Framebuffer;
class Scene;
class Texture2D;

struct UiCanvasStats {
    uint64_t geometryRebuilds = 0;  // elementos cuja geometria foi refeita (desde o início)
    uint64_t listRebuilds = 0;      // frames em que a lista de desenho mudou
    size_t quads = 0;               // no último frame
    size_t batches = 0;             // no último frame (= chamadas de desenho)
};

class UiCanvas {
public:
    // Textura do atlas de uma fonte (o UiRenderer cria na GPU). Sem fonte de texturas: nullptr (testes).
    using AtlasTextureFn = std::function<const Texture2D*(const FontData& font)>;

    void SetAtlasTextureSource(AtlasTextureFn fn) { m_atlasTexture = std::move(fn); }

    // Monta a lista de desenho da cena numa tela desse tamanho (pixels). `assets` acha os arquivos de fonte
    // pelo GUID. Devolve true se a lista mudou desde a última chamada.
    bool Build(const Scene& scene, const AssetLibrary& assets, const glm::vec2& screenSize);

    const DrawList2D& List() const { return m_list; }
    const std::vector<UiNode>& Layout() const { return m_layout; }
    const UiCanvasStats& Stats() const { return m_stats; }

    // Fonte do texto (carrega na primeira vez; arquivo ausente ou inválido = fonte padrão, com aviso no log).
    // nullptr só se nem a fonte padrão abrir.
    const FontData* FontFor(const UiFont& font, const AssetLibrary& assets);

private:
    // Entradas que definem a geometria de um elemento. Igual ao frame anterior = reaproveita os vértices.
    struct ElementKey {
        UiBox box;
        glm::vec4 background{0.0f};
        const Texture2D* texture = nullptr;
        bool hasBackground = false;
        std::string text;
        const FontData* font = nullptr;
        float fontSize = 0.0f;
        glm::vec4 textColor{0.0f};
        UiTextAlign align = UiTextAlign::Center;
        bool operator==(const ElementKey&) const = default;
    };
    struct CachedElement {
        ElementKey key;
        std::vector<Vertex2D> background;  // 4 vértices ou nenhum
        std::vector<Vertex2D> text;
        uint64_t version = 0;
        bool used = false;
    };
    struct Piece {
        EntityId entity = 0;
        uint64_t version = 0;
        bool operator==(const Piece&) const = default;
    };

    std::shared_ptr<FontData> LoadFont(const std::string& key, const std::filesystem::path& file);
    const FontData* DefaultFont();
    void Rebuild(CachedElement& cached, const ElementKey& key, const FontData* font);

    AtlasTextureFn m_atlasTexture;
    std::map<std::string, std::shared_ptr<FontData>> m_fonts;  // caminho UTF-8 -> fonte (nula = falhou)
    std::unordered_map<EntityId, CachedElement> m_cache;
    std::vector<Piece> m_pieces;  // o que formou a lista atual
    std::vector<UiNode> m_layout;
    std::vector<TextQuad> m_quads;  // reaproveitado
    DrawList2D m_list;
    UiCanvasStats m_stats;
    uint64_t m_nextVersion = 0;
};

class UiRenderer {
public:
    UiRenderer();  // exige o contexto OpenGL
    ~UiRenderer();

    // Desenha a UI da cena por cima do alvo (nullptr = janela de width x height).
    void Render(const Scene& scene, const AssetLibrary& assets, int width, int height, const Framebuffer* target = nullptr);

    UiCanvas& Canvas() { return m_canvas; }
    const UiCanvas& Canvas() const { return m_canvas; }
    uint32_t LastDrawCalls() const { return m_renderer.LastDrawCalls(); }

private:
    UiCanvas m_canvas;
    Renderer2D m_renderer;
    std::unordered_map<const FontData*, std::unique_ptr<Texture2D>> m_atlases;
};

} // namespace motor
