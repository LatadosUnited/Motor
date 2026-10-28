#pragma once

// Documento de UI (o papel do UXML do UI Toolkit): a árvore de elementos de uma tela (HUD, menu) num arquivo .mui
// do projeto, ligado à cena por um componente UiDocument. A Hierarquia fica com uma entidade ("Menu"), não com uma
// por texto e botão.
//
//   {"format": "motor-ui", "version": 3, "styles": ["Assets/UI/Tema.mstyle"], "elements": [
//       {"type": "Panel", "name": "menu", "placement": "Absolute", "anchor": "Center", "width": 600,
//        "padding": 30, "gap": 16, "color": [0, 0, 0, 0.6], "children": [
//           {"type": "Text", "name": "titulo", "classes": ["titulo"], "text": "Pausa", "fontSize": 56},
//           {"type": "Button", "name": "jogar", "classes": ["menu"], "text": "Continuar"},
//           {"type": "Button", "name": "sair", "text": "Sair"}]}]}
//
// Estilo (UiStyle.h): `styles` lista as folhas .mstyle do documento (GUID e caminho, ou só o caminho). Cada elemento
// tem o conteúdo (tipo, nome, classes, texto, visible, interactable, imagem, filhos) e as declarações de estilo
// escritas nele (`inlineStyle`: as chaves de estilo do arquivo, como "color" e "width"). Os campos de estilo do
// UiElement (color, width...) são os valores calculados pela cascata (ComputeUiStyles); mudar um deles direto num
// script vale até o próximo cálculo (estado do botão mudou, folha recarregada). Para mudar de vez, SetStyle.
//
// Layout (um subconjunto do flex do CSS, feito aqui; ver UiFlexLayout.cpp): os filhos de um elemento ficam em
// coluna (ou linha, `direction`), um depois do outro, com `gap` entre eles e `padding` dentro do pai; `grow` reparte a
// sobra, `shrink` a falta; `justify` arruma no eixo principal e `alignItems`/`alignSelf` no outro (Stretch, o padrão,
// estica). Tamanhos em pixels da tela de referência, em porcentagem do pai ("50%") ou "auto" (o conteúdo: texto,
// filhos). `placement: Absolute` tira o elemento do fluxo e o prende por âncora no pai, como o UiRect. A raiz do
// documento é uma coluna que ocupa o pai da entidade. `overflow: Hidden` recorta os filhos na caixa; `Scroll` também
// rola na vertical com a roda do mouse.
//
// A ordem na lista é a ordem de desenho (o último fica por cima). Campos ausentes ficam com o padrão do tipo (e das
// folhas); o gravador grava só o que está escrito no elemento. Versões 1 e 2 continuam lidas (as chaves viram
// declarações do elemento). Arquivo da versão 1 (antes do flex): todo elemento é Absolute, com "size" em
// pixels e texto sem quebra. Os scripts acham um elemento pelo nome (UiSystem::WasClicked("jogar"), SetText).

#include "motor/scene/Component.h"
#include "motor/ui/UiComponents.h"
#include "motor/ui/UiLayout.h"
#include "motor/ui/UiStyle.h"

#include <glm/vec4.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

class AssetLibrary;
class FontData;
class Texture2D;

enum class UiElementType {
    Panel,   // fundo com cor (transparente por padrão): agrupa os filhos
    Text,    // só o texto, não bloqueia o clique
    Image,   // retângulo com cor e textura
    Button,  // fundo com a cor do estado, texto e clique
};

const char* UiElementTypeName(UiElementType type);  // gravado no arquivo ("Button")

struct UiElement {
    UiElementType type = UiElementType::Panel;
    std::string name;                  // único no documento, para os scripts ("jogar")
    std::vector<std::string> classes;  // para as consultas e os seletores das folhas (".menu")

    // --- Estilo calculado (a cascata escreve; ver UiStyle.h) ---
    // Lugar no pai: no fluxo (flex) ou preso por âncora (Absolute, como o UiRect: posição a partir da âncora).
    UiPlacement placement = UiPlacement::Flow;
    UiAnchor anchor = UiAnchor::Center;
    glm::vec2 position{0.0f};
    UiLength width;   // automático por padrão (o conteúdo; no fluxo, Stretch estica no eixo cruzado)
    UiLength height;
    UiEdges margin;
    float grow = 0.0f;    // parte da sobra do pai no eixo principal (proporcional entre os irmãos)
    float shrink = 1.0f;  // parte da falta (proporcional ao tamanho)
    UiAlign alignSelf = UiAlign::Auto;

    // Como os filhos se arrumam dentro deste.
    UiDirection direction = UiDirection::Column;
    UiJustify justify = UiJustify::Start;
    UiAlign alignItems = UiAlign::Stretch;
    float gap = 0.0f;
    UiEdges padding;
    UiOverflow overflow = UiOverflow::Visible;

    glm::vec4 color{1.0f};  // fundo do painel e da imagem; no botão, multiplica a cor do estado

    UiFont font;
    std::shared_ptr<const FontData> fontData;  // a de `font`, carregada por ResolveUiDocumentAssets (nula = padrão)
    float fontSize = 36.0f;
    glm::vec4 textColor{1.0f};
    UiTextAlign align = UiTextAlign::Center;
    UiVerticalAlign verticalAlign = UiVerticalAlign::Middle;
    bool wrap = false;  // quebra nos espaços para caber na largura (o padrão do Text)

    glm::vec4 normalColor{0.18f, 0.22f, 0.30f, 0.92f};
    glm::vec4 hoverColor{0.26f, 0.33f, 0.46f, 0.96f};
    glm::vec4 pressedColor{0.12f, 0.15f, 0.21f, 1.0f};

    // Visual (Etapa 4). Pixels da referência, como o resto.
    float radius = 0.0f;       // cantos arredondados do fundo
    float borderWidth = 0.0f;  // borda por dentro da caixa
    glm::vec4 borderColor{0.0f, 0.0f, 0.0f, 1.0f};
    UiEdges slice;             // 9-slice da imagem: bordas em pixels da textura que não esticam
    float opacity = 1.0f;      // multiplica o alfa deste e dos filhos
    float textOutlineWidth = 0.0f;
    glm::vec4 textOutlineColor{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec2 textShadowOffset{0.0f};                   // para a direita e para baixo
    glm::vec4 textShadowColor{0.0f, 0.0f, 0.0f, 0.0f};  // alfa 0: sem sombra
    float scale = 1.0f;        // só no desenho, em volta do centro (o layout e o clique não mudam)
    float transition = 0.0f;   // segundos para cor, opacidade e escala chegarem ao valor novo (no Play)

    // --- Conteúdo ---
    bool visible = true;
    UiFont image;                        // textura (.png...) do projeto, pelo GUID; vazio = cor sólida
    std::shared_ptr<Texture2D> texture;  // a de `image`, carregada por ResolveUiDocumentAssets
    std::string text;                    // texto do Text e rótulo do Button
    bool interactable = true;

    // Declarações escritas no próprio elemento (as chaves de estilo do .mui): ganham das folhas.
    UiStyleDeclarations inlineStyle;

    std::vector<UiElement> children;

    // Estado do jogo, escrito pelo UiSystem, pelo layout e pela cascata (não vai para o arquivo).
    bool hovered = false;
    bool pressed = false;
    bool clicked = false;
    bool focused = false;     // :focus (a navegação por foco vem na Etapa 6)
    float scroll = 0.0f;      // overflow Scroll: quanto rolou para baixo (pixels da referência)
    float scrollMax = 0.0f;   // o conteúdo que passa da caixa (calculado pelo layout)
    uint8_t styleStates = 0;  // os estados (kUiState*) do último cálculo dos estilos
    bool styleDirty = false;  // inlineStyle mudou: calcular de novo
    UiTransitionState animation;

    // Elemento com os padrões do tipo (o painel começa transparente, o texto com "Texto").
    static UiElement Make(UiElementType type);

    // Muda uma declaração do elemento (como no arquivo: SetStyle("color", "[1, 0, 0, 1]")) e o campo calculado na
    // hora; vale também depois do próximo cálculo dos estilos. false: propriedade desconhecida ou valor inválido.
    bool SetStyle(std::string_view property, std::string_view json);
    bool SetStyleValue(std::string_view property, const UiStyleValue& value);
    bool ClearStyle(std::string_view property);  // tira a declaração (vale no próximo cálculo dos estilos)

    bool HasBackground() const;  // desenha um retângulo (painel e imagem só se visíveis; texto nunca)
    bool BlocksPointer() const;  // pega o clique: botão e imagem; painel com fundo visível
    glm::vec4 BackgroundColor() const;  // com a cor do estado no botão
    bool HasText() const { return (type == UiElementType::Text || type == UiElementType::Button) && !text.empty(); }
    const FontData* Font() const;  // a fonte do texto (a padrão se não houver outra)
    UiVisual Visual() const;       // fundo, texto, borda, opacidade e escala calculados
    UiVisual ShownVisual() const { return animation.active ? animation.now : Visual(); }  // no meio da transição

    // Conteúdo e declarações iguais (o estado do jogo, os valores calculados e o que foi carregado não contam).
    bool SameContent(const UiElement& other) const;
};

// Guarda o último layout de um documento (UiFlexLayout.cpp). Cópia começa vazia.
struct UiLayoutCache {
    uint64_t signature = 0;
    bool valid = false;
    std::vector<UiNode> nodes;  // `parent` relativo ao primeiro nó

    UiLayoutCache() = default;
    UiLayoutCache(const UiLayoutCache&) {}
    UiLayoutCache& operator=(const UiLayoutCache&) {
        valid = false;
        nodes.clear();
        return *this;
    }
};

// O conteúdo de um .mui.
struct UiDocumentData {
    std::vector<UiElement> elements;
    std::vector<UiFont> styles;  // folhas .mstyle (GUID e caminho), na ordem: a de depois ganha nos empates
    std::vector<std::shared_ptr<const UiStyleSheet>> sheets;  // as de `styles`, lidas por ResolveUiDocumentAssets
    std::map<std::string, std::shared_ptr<const FontData>> fonts;  // fontes das declarações, por GUID e por caminho

    // Estado (não vai para o arquivo).
    // Campos calculados pela cascata: lido de arquivo, ou depois de ComputeUiStyles. Documento montado em código
    // (false) usa os campos como estão, e o gravador grava os diferentes do padrão do tipo.
    bool styled = false;
    bool styleDirty = false;  // folhas mudaram: calcular de novo
    bool animate = false;     // o UiSystem anda as transições (Play); sem ele, os valores mudam na hora
    uint64_t styleRuns = 0;   // quantas vezes a cascata rodou (testes, desempenho)
    uint64_t layoutRuns = 0;  // quantas vezes o layout foi feito de verdade (não veio do cache)
    UiLayoutCache layoutCache;

    // Conteúdo, declarações e folhas iguais.
    bool SameContent(const UiDocumentData& other) const;
    UiElement* Find(std::string_view name);  // em profundidade; nullptr se nenhum
    const UiElement* Find(std::string_view name) const;
};

inline constexpr const char* kUiDocumentFormat = "motor-ui";
inline constexpr int kUiDocumentVersion = 3;  // 3: folhas de estilo (2: layout flex; 1: só âncoras)

// Cascata: recalcula os campos de estilo de todos os elementos (padrão do tipo, folha padrão da engine, folhas do
// documento por especificidade e ordem, declarações do elemento; herança de textColor, font, fontSize e align) e marca
// o documento como `styled`. Começa as transições quando `animate`.
void ComputeUiStyles(UiDocumentData& document);
// Recalcula só se algo mudou (estado de um elemento, declarações, folhas). Documento não `styled`: nada. true se
// recalculou. O layout chama a cada vez; custa uma volta pela árvore.
bool UpdateUiStyles(UiDocumentData& document);
// Anda as transições em andamento (o UiSystem, a cada passo do jogo).
void AdvanceUiTransitions(UiDocumentData& document, float dt);

// Texto JSON <-> documento. A leitura é tolerante (campo ausente = padrão, tipo desconhecido = painel, com aviso em
// `warnings`); falha só com JSON inválido, outro formato ou versão mais nova que esta engine.
std::string UiDocumentToJson(const UiDocumentData& document);
bool UiDocumentFromJson(std::string_view json, UiDocumentData& out, std::string* error = nullptr,
                        std::vector<std::string>* warnings = nullptr);
bool SaveUiDocument(const std::filesystem::path& file, const UiDocumentData& document, std::string* error = nullptr);
bool LoadUiDocument(const std::filesystem::path& file, UiDocumentData& out, std::string* error = nullptr);

// Acerta as folhas, fontes e imagens pelo GUID (arquivo movido), carrega as folhas, as texturas e as fontes e
// recalcula os estilos.
void ResolveUiDocumentAssets(UiDocumentData& document, AssetLibrary& assets);

// Referência a um .mui do projeto, com o conteúdo lido junto (como uma textura: carregado ao ler a cena ou ao
// escolher o arquivo no Inspector). Igualdade só pelo arquivo.
struct UiDocumentRef {
    std::string guid;
    std::string path;  // relativo ao projeto ("Assets/UI/Menu.mui")
    std::shared_ptr<const UiDocumentData> data;  // nulo: sem arquivo, ou não abriu (aviso no log)

    bool IsEmpty() const { return guid.empty() && path.empty(); }
    bool operator==(const UiDocumentRef& other) const { return guid == other.guid && path == other.path; }
};

// Liga a referência a um arquivo (chave "Assets/..."), lendo o documento. Vazio = sem documento.
void SetUiDocumentFile(UiDocumentRef& ref, std::string_view key, AssetLibrary& assets);
// Lê de novo o arquivo já ligado e as folhas dele (depois de gravar o .mui ou um .mstyle).
void ReloadUiDocument(UiDocumentRef& ref, AssetLibrary& assets);

template <> struct FieldTraits<UiDocumentRef> {
    static CustomFieldType Describe();
};

// Cópia do documento que o jogo muda (texto, visível, estado dos botões). Copiar o componente (Play, desfazer)
// não leva a cópia: ela é refeita do documento na primeira vez que alguém pedir.
class UiDocumentInstance {
public:
    UiDocumentInstance() = default;
    UiDocumentInstance(const UiDocumentInstance&) {}
    UiDocumentInstance& operator=(const UiDocumentInstance&) {
        m_tree.reset();
        m_from.reset();
        return *this;
    }

    // A árvore viva do documento (refeita se o documento ligado mudou). nullptr sem documento.
    UiDocumentData* Get(const UiDocumentRef& ref);

private:
    std::unique_ptr<UiDocumentData> m_tree;
    std::shared_ptr<const UiDocumentData> m_from;  // de onde a cópia saiu (segura o endereço: não é reaproveitado)
};

// Componente: a entidade mostra o documento. Ocupa o pai inteiro (a tela, ou o retângulo de um elemento de UI pai);
// entre irmãos, `order` maior desenha por cima, como o UiRect.
struct UiDocument {
    UiDocumentRef document;
    int order = 0;
    bool visible = true;

    mutable UiDocumentInstance instance;  // estado do jogo (não registrado)

    UiDocumentData* Tree() const { return instance.Get(document); }
};

} // namespace motor
