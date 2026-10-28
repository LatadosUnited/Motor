#pragma once

// Componentes da interface do jogo (HUD, menus). São componentes comuns (Component.h): aparecem no Inspector,
// vão para a cena e entram no desfazer. A UI é desenhada por cima da cena, em pixels de uma tela de
// referência com kUiReferenceHeight de altura (UiLayout.h): o HUD fica proporcional em qualquer janela.
//
//   Entidade "Vida"   + UiRect (âncora TopLeft, posição 40,40)  + UiText ("Vida: 3")
//   Entidade "Jogar"  + UiRect (Center, 320x90) + UiButton + UiText ("Jogar")
//
// Uma entidade de UI filha de outra se posiciona dentro do retângulo do pai (um painel com botões); filha de
// uma entidade que não é de UI, dentro da tela. Esconder (UiRect::visible) esconde também os filhos.
// Elementos sem UiRect usam o retângulo padrão (centro, 200x60).

#include "motor/scene/Component.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <string>

namespace motor {

struct Entity;
class Texture2D;

// Ponto da tela (ou do pai) onde o elemento se prende. É também o ponto do próprio elemento que fica nele:
// TopRight prende o canto superior direito do elemento no canto superior direito da tela.
// Stretch: ocupa o pai inteiro menos as margens (posição = esquerda/cima, tamanho = direita/baixo).
enum class UiAnchor { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight, Stretch };
enum class UiTextAlign { Left, Center, Right };
enum class UiVerticalAlign { Top, Middle, Bottom };  // texto na caixa (documentos de UI)

// Fonte de um texto: um .ttf/.otf do projeto, pelo GUID (com o caminho junto, como plano B). Vazio = fonte
// padrão da engine (Roboto Medium).
struct UiFont {
    std::string guid;
    std::string path;  // relativo ao projeto ("Assets/Fontes/Titulo.ttf")

    bool IsDefault() const { return guid.empty() && path.empty(); }
    bool operator==(const UiFont&) const = default;
};

// Posição e tamanho na tela.
struct UiRect {
    UiAnchor anchor = UiAnchor::Center;
    glm::vec2 position{0.0f};       // pixels a partir da âncora: x para a direita, y para baixo
    glm::vec2 size{200.0f, 60.0f};  // pixels (Stretch: margens direita e de baixo)
    int order = 0;                  // entre irmãos, maior desenha por cima (e recebe o clique primeiro)
    bool visible = true;            // falso esconde o elemento e os filhos
};

// Retângulo com cor e, opcionalmente, uma textura.
struct UiImage {
    std::shared_ptr<Texture2D> texture;  // nula = cor sólida
    glm::vec4 color{1.0f};
};

struct UiText {
    std::string text = "Texto";
    UiFont font;
    float fontSize = 36.0f;  // pixels da tela de referência
    glm::vec4 color{1.0f};
    UiTextAlign align = UiTextAlign::Center;  // na horizontal; na vertical o texto fica centrado
};

// Botão: fundo com a cor do estado (multiplica a UiImage da entidade, se houver) e clique detectado pelo
// UiSystem durante o jogo.
struct UiButton {
    glm::vec4 normalColor{0.18f, 0.22f, 0.30f, 0.92f};
    glm::vec4 hoverColor{0.26f, 0.33f, 0.46f, 0.96f};
    glm::vec4 pressedColor{0.12f, 0.15f, 0.21f, 1.0f};
    bool interactable = true;

    // Estado do jogo, escrito pelo UiSystem (não é campo registrado: não vai para a cena nem para o desfazer).
    bool hovered = false;
    bool pressed = false;
    bool clicked = false;  // soltou o clique nele no último frame (UiSystem::WasClicked)

    const glm::vec4& CurrentColor() const;
};

template <> struct FieldTraits<UiAnchor> {
    static CustomFieldType Describe();
};
template <> struct FieldTraits<UiTextAlign> {
    static CustomFieldType Describe();
};
template <> struct FieldTraits<UiFont> {
    static CustomFieldType Describe();
};

const char* UiAnchorName(UiAnchor anchor);    // gravado na cena ("TopLeft")
const char* UiAnchorLabel(UiAnchor anchor);   // no editor ("Em cima, à esquerda")
const char* UiTextAlignName(UiTextAlign align);
const char* UiTextAlignLabel(UiTextAlign align);

// A entidade tem algum componente de UI (UiRect, UiImage, UiText, UiButton ou UiDocument).
bool IsUiEntity(const Entity& entity);

} // namespace motor
