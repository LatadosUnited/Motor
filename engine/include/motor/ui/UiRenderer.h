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
//
// Cantos arredondados sem mexer no shader: um círculo branco com borda suave numa textura pequena (feita na CPU,
// MakeUiCirclePixels), desenhado em 9 pedaços (os cantos são quartos do círculo; as bordas e o meio, o centro
// dele, que é branco). A borda é a mesma forma na cor da borda por baixo do fundo, encolhido pela largura dela.

#include "motor/renderer/Renderer2D.h"
#include "motor/ui/Font.h"
#include "motor/ui/UiLayout.h"
#include "motor/ui/UiStyle.h"

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
struct UiElement;

struct UiCanvasStats {
    uint64_t geometryRebuilds = 0;  // elementos cuja geometria foi refeita (desde o início)
    uint64_t listRebuilds = 0;      // frames em que a lista de desenho mudou
    size_t quads = 0;               // no último frame
    size_t batches = 0;             // no último frame (= chamadas de desenho)
};

inline constexpr int kUiCircleTextureSize = 128;  // lado da textura do círculo dos cantos arredondados

// Pixels RGBA do círculo branco (alfa = cobertura do pixel, borda suave de 1 pixel), size x size.
std::vector<uint8_t> MakeUiCirclePixels(int size = kUiCircleTextureSize);

// 9-slice: `box` em até 9 quadrados com a cor `color`. `border` são as larguras das bordas na tela (pixels; os
// cantos não esticam) e `uvBorder` as mesmas bordas em fração da textura (0 a 1), medidas a partir de uvMin/uvMax.
// Bordas maiores que a caixa encolhem na proporção. Pedaço vazio não gera quadrado; `clip` recorta.
void AppendUiNineSlice(std::vector<Vertex2D>& out, const UiBox& box, const UiEdges& border, glm::vec2 uvMin,
                       glm::vec2 uvMax, const UiEdges& uvBorder, const glm::vec4& color, const UiBox* clip = nullptr);

class UiCanvas {
public:
    // Textura do atlas de uma fonte (o UiRenderer cria na GPU). Sem fonte de texturas: nullptr (testes).
    using AtlasTextureFn = std::function<const Texture2D*(const FontData& font)>;
    // Textura do círculo dos cantos arredondados (MakeUiCirclePixels). Sem ela: nullptr (testes; a geometria é a mesma).
    using ShapeTextureFn = std::function<const Texture2D*()>;

    void SetAtlasTextureSource(AtlasTextureFn fn) { m_atlasTexture = std::move(fn); }
    void SetShapeTextureSource(ShapeTextureFn fn) { m_shapeTexture = std::move(fn); }

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
    // Pedaço a mais de um controle (parte cheia, trilho, puxador, caixa e marca, cursor), por cima do fundo.
    struct ElementPart {
        UiBox box;
        glm::vec4 color{0.0f};
        float radius = 0.0f;
        bool operator==(const ElementPart&) const = default;
    };
    // Entradas que definem a geometria de um elemento. Igual ao frame anterior = reaproveita os vértices.
    struct ElementKey {
        UiBox box;  // já com a escala de desenho (scale) do elemento e dos pais
        glm::vec4 background{0.0f};
        const Texture2D* texture = nullptr;
        glm::vec2 textureSize{0.0f};
        bool hasBackground = false;
        float radius = 0.0f;  // pixels da tela, como as outras medidas daqui
        float borderWidth = 0.0f;
        glm::vec4 borderColor{0.0f};
        UiEdges slice;          // pixels da textura
        float slicePixel = 1.0f;  // tamanho na tela de um pixel da textura no 9-slice
        float opacity = 1.0f;     // do elemento e dos pais
        std::string text;
        const FontData* font = nullptr;
        float fontSize = 0.0f;
        glm::vec4 textColor{0.0f};
        UiTextAlign align = UiTextAlign::Center;
        UiBox textBox;          // dentro do padding
        float wrapWidth = 0.0f;  // > 0: quebra o texto nessa largura
        UiVerticalAlign vertical = UiVerticalAlign::Middle;
        float outlineWidth = 0.0f;
        glm::vec4 outlineColor{0.0f};
        glm::vec2 shadowOffset{0.0f};
        glm::vec4 shadowColor{0.0f};
        UiBox clip;             // recorte (overflow Hidden/Scroll de um pai), se `clipped`
        bool clipped = false;
        std::vector<ElementPart> parts;  // controles (Etapa 6)
        bool operator==(const ElementKey&) const = default;
    };
    // Vértices seguidos com a mesma textura (a borda, o fundo, o texto).
    struct Segment {
        const Texture2D* texture = nullptr;
        std::vector<Vertex2D> vertices;
    };
    struct CachedElement {
        ElementKey key;
        std::vector<Segment> segments;
        uint64_t version = 0;
        bool used = false;
    };
    // Um elemento desenhado: a entidade de UI, ou um elemento do documento dela.
    struct PieceId {
        EntityId entity = 0;
        const UiElement* element = nullptr;
        bool operator==(const PieceId&) const = default;
    };
    struct PieceIdHash {
        size_t operator()(const PieceId& id) const {
            return std::hash<const void*>()(id.element) ^ (std::hash<EntityId>()(id.entity) * 0x9e3779b97f4a7c15ull);
        }
    };
    struct Piece {
        PieceId id;
        uint64_t version = 0;
        bool operator==(const Piece&) const = default;
    };

    std::shared_ptr<const FontData> LoadFont(const std::string& key, const std::filesystem::path& file);
    const FontData* DefaultFont();
    void Rebuild(CachedElement& cached, const ElementKey& key, const FontData* font);
    // Trilho, parte cheia, puxador, caixa e marca, cursor (key.textBox já é o conteúdo; muda no Toggle).
    void AddControlParts(const UiElement& element, ElementKey& key, float pixel, const FontData* font);

    AtlasTextureFn m_atlasTexture;
    ShapeTextureFn m_shapeTexture;
    std::map<std::string, std::shared_ptr<const FontData>> m_fonts;  // caminho UTF-8 -> fonte (nula = falhou)
    std::unordered_map<PieceId, CachedElement, PieceIdHash> m_cache;
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
    std::unique_ptr<Texture2D> m_circle;
};

} // namespace motor
