#pragma once

// Folha de estilo da UI (o papel do USS do UI Toolkit): um arquivo .mstyle do projeto com regras que mudam o visual
// dos elementos dos documentos (.mui) que a usam. Mudar a cor de todos os botões é mudar uma regra.
//
//   {"format": "motor-style", "version": 1, "rules": [
//       {"selector": "Button", "radius": 12, "borderWidth": 2, "borderColor": [1, 1, 1, 0.4], "transition": 0.15},
//       {"selector": "Button:hover", "normalColor": [0.3, 0.4, 0.6, 1]},
//       {"selector": ".titulo, #placar", "fontSize": 64, "textOutlineWidth": 2, "textOutlineColor": [0, 0, 0, 1]}]}
//
// Seletor: composto só (sem descendente nem filho): tipo opcional (Panel, Text, Image, Button, ou *), `.classe` (quantas
// quiser), `#nome` e estados `:hover` (mouse em cima), `:active` (apertado), `:disabled` (interactable falso) e
// `:focus`. Lista com vírgula vale para cada seletor. Especificidade como no CSS (#nome, depois classes e estados,
// depois tipo); empate: a regra que vem depois ganha (e a folha que vem depois na lista do documento).
//
// As propriedades são as mesmas do .mui, com os mesmos nomes e formas no JSON (UiStylePropertyName): lugar e
// tamanho (placement, anchor, position, width, height, margin, grow, shrink, alignSelf), arrumação dos filhos
// (direction, justify, alignItems, gap, padding, overflow), cores e texto (color, font, fontSize, textColor, align,
// verticalAlign, wrap, normalColor, hoverColor, pressedColor) e o visual da Etapa 4: radius, borderWidth,
// borderColor, slice (9-slice da imagem, em pixels da textura), opacity (0 a 1, vale para os filhos),
// textOutlineWidth, textOutlineColor, textShadowOffset, textShadowColor, scale (só no desenho, em volta do centro) e
// transition (segundos: cor, opacidade e escala mudam aos poucos entre os estados, no Play).
//
// Cascata (ComputeUiStyles, em UiDocument.h): padrão do tipo (UiElement::Make) < folha padrão da engine < folhas do
// documento (por especificidade, depois ordem) < declarações no próprio elemento (`inlineStyle`). textColor, font,
// fontSize e align vêm do pai quando nada as define (herança, como as propriedades de texto do CSS).

#include "motor/ui/UiComponents.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace motor {

struct UiElement;

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
    bool IsZero() const { return top == 0.0f && right == 0.0f && bottom == 0.0f && left == 0.0f; }
    bool operator==(const UiEdges&) const = default;
};

enum class UiPlacement { Flow, Absolute };  // no fluxo do pai, ou preso por âncora
enum class UiDirection { Column, Row };
enum class UiJustify { Start, Center, End, SpaceBetween, SpaceAround };
enum class UiAlign { Auto, Start, Center, End, Stretch };  // Auto: só em alignSelf (usa o alignItems do pai)
enum class UiOverflow { Visible, Hidden, Scroll };

// --- Propriedades de estilo ----------------------------------------------------------------------------------------

// Valor de uma propriedade: bool, número, vec2, cor (vec4), tamanho, lados, enum (como int) ou arquivo (fonte).
using UiStyleValue = std::variant<bool, float, glm::vec2, glm::vec4, UiLength, UiEdges, int, UiFont>;

// A tabela das propriedades (uma só, usada pelo .mui e pelo .mstyle). Índice -1 = desconhecida.
int UiStylePropertyCount();
int FindUiStyleProperty(std::string_view name);
const char* UiStylePropertyName(int property);  // "color"; "" fora da tabela
bool UiStylePropertyInherited(int property);    // vem do pai quando nada a define (textColor, font, fontSize, align)
// Nomes dos valores de uma propriedade enum, na ordem dos valores ("Flow", "Absolute"); vazio se não for enum.
std::vector<std::string_view> UiStylePropertyEnumNames(int property);

// Valor <-> texto JSON ("[1, 0, 0, 1]", "\"50%\"", "12"). Leitura falha com valor da forma errada.
bool ParseUiStyleValue(int property, std::string_view json, UiStyleValue& out);
std::string UiStyleValueToJson(int property, const UiStyleValue& value);
// O campo do elemento que guarda a propriedade.
UiStyleValue GetUiStyleValue(const UiElement& element, int property);
void SetUiStyleValue(UiElement& element, int property, const UiStyleValue& value);

struct UiStyleDeclaration {
    int property = -1;
    UiStyleValue value;
    bool operator==(const UiStyleDeclaration&) const = default;
};

// Declarações de uma regra ou de um elemento: uma por propriedade, na ordem da tabela.
struct UiStyleDeclarations {
    std::vector<UiStyleDeclaration> items;

    void Set(int property, const UiStyleValue& value);
    bool Set(std::string_view property, std::string_view json);  // false: propriedade desconhecida ou valor inválido
    bool Remove(std::string_view property);
    const UiStyleValue* Find(int property) const;
    bool Empty() const { return items.empty(); }
    bool operator==(const UiStyleDeclarations&) const = default;
};

// --- Seletores -----------------------------------------------------------------------------------------------------

// Estados do elemento que os seletores olham (bits).
enum : uint8_t {
    kUiStateHover = 1 << 0,     // :hover
    kUiStateActive = 1 << 1,    // :active (apertado)
    kUiStateDisabled = 1 << 2,  // :disabled (interactable falso)
    kUiStateFocus = 1 << 3,     // :focus
};
uint8_t UiElementStates(const UiElement& element);

struct UiSelector {
    std::string type;                  // "Button"; vazio = qualquer (ou "*")
    std::vector<std::string> classes;  // todas precisam estar no elemento
    std::string name;                  // #nome; vazio = qualquer
    uint8_t states = 0;                // todos precisam estar ligados

    // (#nome, classes + estados, tipo) num número só, para comparar como no CSS.
    int Specificity() const;
    bool Matches(const UiElement& element, uint8_t elementStates) const;
    bool operator==(const UiSelector&) const = default;
};

// "Button.menu:hover, #sair" -> um seletor por item da lista. Falha com estado ou tipo desconhecido, combinador
// (espaço, >, +, ~) ou item vazio.
bool ParseUiSelectors(std::string_view text, std::vector<UiSelector>& out, std::string* error = nullptr);

// --- Folha ---------------------------------------------------------------------------------------------------------

struct UiStyleRule {
    std::string selector;                // como no arquivo
    std::vector<UiSelector> selectors;   // o mesmo, lido
    UiStyleDeclarations declarations;
    bool operator==(const UiStyleRule&) const = default;
};

struct UiStyleSheet {
    std::vector<UiStyleRule> rules;
    bool operator==(const UiStyleSheet&) const = default;
};

inline constexpr const char* kUiStyleFormat = "motor-style";
inline constexpr int kUiStyleVersion = 1;

// Texto JSON <-> folha. A leitura é tolerante (regra com seletor inválido, propriedade desconhecida ou valor da forma
// errada ficam de fora, com aviso em `warnings`); falha só com JSON inválido, outro formato ou versão mais nova.
std::string UiStyleSheetToJson(const UiStyleSheet& sheet);
bool UiStyleSheetFromJson(std::string_view json, UiStyleSheet& out, std::string* error = nullptr,
                          std::vector<std::string>* warnings = nullptr);
bool SaveUiStyleSheet(const std::filesystem::path& file, const UiStyleSheet& sheet, std::string* error = nullptr);
bool LoadUiStyleSheet(const std::filesystem::path& file, UiStyleSheet& out, std::string* error = nullptr);

// A folha padrão da engine (o visual de sempre: painel transparente, botão com padding e as cores dos estados). Vem
// antes das folhas dos documentos: qualquer regra delas ganha desta.
const UiStyleSheet& DefaultUiStyleSheet();

// Valores que a transição anima (o fundo já com a cor do estado do botão).
struct UiVisual {
    glm::vec4 background{0.0f};
    glm::vec4 textColor{0.0f};
    glm::vec4 borderColor{0.0f};
    float opacity = 1.0f;
    float scale = 1.0f;
    bool operator==(const UiVisual&) const = default;
};

// Transição em andamento num elemento (estado do jogo, não vai para o arquivo).
struct UiTransitionState {
    UiVisual from, to, now;  // `to`: o alvo do último cálculo dos estilos
    float time = 0.0f;
    float duration = 0.0f;
    bool active = false;
    bool started = false;  // `to` já foi calculado uma vez (o primeiro cálculo não anima)
};

} // namespace motor
