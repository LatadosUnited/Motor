#pragma once

// Documento de UI (o papel do UXML do UI Toolkit): a árvore de elementos de uma tela (HUD, menu) num arquivo .mui
// do projeto, ligado à cena por um componente UiDocument. A Hierarquia fica com uma entidade ("Menu"), não com uma
// por texto e botão.
//
//   {"format": "motor-ui", "version": 2, "elements": [
//       {"type": "Panel", "name": "menu", "placement": "Absolute", "anchor": "Center", "width": 600,
//        "padding": 30, "gap": 16, "color": [0, 0, 0, 0.6], "children": [
//           {"type": "Text", "name": "titulo", "text": "Pausa", "fontSize": 56},
//           {"type": "Button", "name": "jogar", "classes": ["menu"], "text": "Continuar"},
//           {"type": "Button", "name": "sair", "text": "Sair"}]}]}
//
// Layout (um subconjunto do flex do CSS, feito aqui; ver UiFlexLayout.cpp): os filhos de um elemento ficam em
// coluna (ou linha, `direction`), um depois do outro, com `gap` entre eles e `padding` dentro do pai; `grow` reparte a
// sobra, `shrink` a falta; `justify` arruma no eixo principal e `alignItems`/`alignSelf` no outro (Stretch, o padrão,
// estica). Tamanhos em pixels da tela de referência, em porcentagem do pai ("50%") ou "auto" (o conteúdo: texto,
// filhos). `placement: Absolute` tira o elemento do fluxo e o prende por âncora no pai, como o UiRect. A raiz do
// documento é uma coluna que ocupa o pai da entidade. `overflow: Hidden` recorta os filhos na caixa; `Scroll` também
// rola na vertical com a roda do mouse.
//
// A ordem na lista é a ordem de desenho (o último fica por cima). Campos ausentes ficam com o padrão do tipo; só os
// diferentes do padrão são gravados. Arquivo da versão 1 (antes do flex): todo elemento é Absolute, com "size" em
// pixels e texto sem quebra. Os scripts acham um elemento pelo nome (UiSystem::WasClicked("jogar"), SetText).

#include "motor/scene/Component.h"
#include "motor/ui/UiComponents.h"

#include <glm/vec4.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

class AssetLibrary;
class FontData;
class Texture2D;

// Medida de um tamanho: pixels da tela de referência, porcentagem do pai ou automática (o conteúdo).
enum class UiUnit { Auto, Px, Percent };
struct UiLength {
    float value = 0.0f;
    UiUnit unit = UiUnit::Auto;

    static UiLength Px(float v) { return {v, UiUnit::Px}; }
    static UiLength Percent(float v) { return {v, UiUnit::Percent}; }
    bool IsAuto() const { return unit == UiUnit::Auto; }
    bool operator==(const UiLength&) const = default;
};

// Margem e preenchimento, em pixels da referência (no arquivo: um número para os 4 lados, [vertical, horizontal]
// ou [cima, direita, baixo, esquerda], como no CSS).
struct UiEdges {
    float top = 0.0f, right = 0.0f, bottom = 0.0f, left = 0.0f;

    static UiEdges All(float v) { return {v, v, v, v}; }
    static UiEdges Symmetric(float vertical, float horizontal) { return {vertical, horizontal, vertical, horizontal}; }
    bool operator==(const UiEdges&) const = default;
};

enum class UiPlacement { Flow, Absolute };  // no fluxo do pai, ou preso por âncora
enum class UiDirection { Column, Row };
enum class UiJustify { Start, Center, End, SpaceBetween, SpaceAround };
enum class UiAlign { Auto, Start, Center, End, Stretch };  // Auto: só em alignSelf (usa o alignItems do pai)
enum class UiOverflow { Visible, Hidden, Scroll };

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
    std::vector<std::string> classes;  // para as consultas e os estilos (etapas seguintes)

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
    bool visible = true;

    // Como os filhos se arrumam dentro deste.
    UiDirection direction = UiDirection::Column;
    UiJustify justify = UiJustify::Start;
    UiAlign alignItems = UiAlign::Stretch;
    float gap = 0.0f;
    UiEdges padding;
    UiOverflow overflow = UiOverflow::Visible;

    glm::vec4 color{1.0f};  // fundo do painel e da imagem; no botão, multiplica a cor do estado
    UiFont image;           // textura (.png...) do projeto, pelo GUID; vazio = cor sólida
    std::shared_ptr<Texture2D> texture;  // a de `image`, carregada por ResolveUiDocumentAssets

    std::string text;  // texto do Text e rótulo do Button
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
    bool interactable = true;

    std::vector<UiElement> children;

    // Estado do jogo, escrito pelo UiSystem e pelo layout (não vai para o arquivo).
    bool hovered = false;
    bool pressed = false;
    bool clicked = false;
    float scroll = 0.0f;     // overflow Scroll: quanto rolou para baixo (pixels da referência)
    float scrollMax = 0.0f;  // o conteúdo que passa da caixa (calculado pelo layout)

    // Elemento com os padrões do tipo (o painel começa transparente, o texto com "Texto").
    static UiElement Make(UiElementType type);

    bool HasBackground() const;  // desenha um retângulo (painel e imagem só se visíveis; texto nunca)
    bool BlocksPointer() const;  // pega o clique: botão e imagem; painel com fundo visível
    glm::vec4 BackgroundColor() const;  // com a cor do estado no botão
    bool HasText() const { return (type == UiElementType::Text || type == UiElementType::Button) && !text.empty(); }
    const FontData* Font() const;  // a fonte do texto (a padrão se não houver outra)

    // Campos do arquivo iguais (o estado do jogo e a textura carregada não contam).
    bool SameContent(const UiElement& other) const;
};

// O conteúdo de um .mui.
struct UiDocumentData {
    std::vector<UiElement> elements;

    bool SameContent(const UiDocumentData& other) const;
    UiElement* Find(std::string_view name);  // em profundidade; nullptr se nenhum
    const UiElement* Find(std::string_view name) const;
};

inline constexpr const char* kUiDocumentFormat = "motor-ui";
inline constexpr int kUiDocumentVersion = 2;  // 2: layout flex (1: só âncoras)

// Texto JSON <-> documento. A leitura é tolerante (campo ausente = padrão, tipo desconhecido = painel, com aviso em
// `warnings`); falha só com JSON inválido, outro formato ou versão mais nova que esta engine.
std::string UiDocumentToJson(const UiDocumentData& document);
bool UiDocumentFromJson(std::string_view json, UiDocumentData& out, std::string* error = nullptr,
                        std::vector<std::string>* warnings = nullptr);
bool SaveUiDocument(const std::filesystem::path& file, const UiDocumentData& document, std::string* error = nullptr);
bool LoadUiDocument(const std::filesystem::path& file, UiDocumentData& out, std::string* error = nullptr);

// Acerta as fontes e imagens pelo GUID (arquivo movido) e carrega as texturas e as fontes.
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
// Lê de novo o arquivo já ligado (depois de gravar o .mui).
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
