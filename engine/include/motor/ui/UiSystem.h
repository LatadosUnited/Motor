#pragma once

// Sistema da UI no jogo (RuntimeSystem, ordem kSystemOrderUi: roda depois dos scripts). A cada OnUpdate vê
// onde está o mouse, acende os botões sob ele e detecta os cliques; os scripts leem no OnUpdate seguinte.
//
// Num script:
//   if (auto* ui = GetSystem<motor::UiSystem>()) {
//       ui->SetText("Pontos", std::format("Pontos: {}", pontos));
//       if (ui->WasClicked("Jogar")) LoadScene("Fase1");
//   }
//
// Clique: apertar e soltar o botão esquerdo em cima do mesmo botão (UiButton clicável). O elemento de cima
// ganha: uma imagem por cima de um botão bloqueia o clique; texto sozinho não bloqueia.
//
// Documentos de UI (UiDocument.h): os mesmos nomes valem para os elementos deles. Sem entidade com o nome, as
// funções procuram um elemento com esse nome nos documentos da cena (WasClicked("jogar"), SetText("pontos", ...)).
// A roda do mouse rola o elemento com overflow Scroll sob o mouse (UiElement::scroll; um script também pode mudar).
//
// Clique da interface não vai para o jogo: antes dos scripts (UiPointerSystem), a UI diz ao runtime se o mouse
// está sobre um botão ou imagem e se o clique começou num deles; até soltar, o botão esquerdo fica solto para
// os scripts (IsMouseDown, ações). Com o cursor preso (Locked), a interface não pega o mouse.

#include "motor/scene/RuntimeSystem.h"
#include "motor/ui/UiLayout.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

class Scene;
struct Entity;
struct UiElement;

inline constexpr float kUiScrollStep = 60.0f;  // pixels da referência por entalhe da roda numa lista com rolagem

// Mouse em relação à tela do jogo, informado por quem roda o jogo (no editor, o viewport) a cada frame.
struct UiPointer {
    glm::vec2 screenSize{0.0f};  // pixels da imagem do jogo
    glm::vec2 position{0.0f};    // pixels, a partir do canto superior esquerdo dela
    bool inside = false;         // o mouse está sobre a imagem do jogo (e o jogo recebe o mouse)
    bool down = false;           // botão esquerdo apertado
};

class UiSystem final : public RuntimeSystem {
public:
    void OnStart(SceneRuntime& runtime) override;
    void OnUpdate(SceneRuntime& runtime, float dt) override;
    void OnStop(SceneRuntime& runtime) override;
    // Antes dos scripts (UiPointerSystem): o mouse sobre a interface e o clique que é dela.
    void UpdatePointer(SceneRuntime& runtime);

    // --- Quem roda o jogo ---
    void SetPointer(const UiPointer& pointer) { m_pointer = pointer; }
    const UiPointer& Pointer() const { return m_pointer; }

    // --- Scripts: pelo nome da entidade (a primeira com esse nome) ou pelo id ---
    // Muda o texto do UiText. false se a entidade não existe ou não tem UiText.
    bool SetText(std::string_view entity, std::string_view text);
    bool SetText(EntityId entity, std::string_view text);
    std::string GetText(std::string_view entity) const;  // "" se não houver
    // Mostra ou esconde o elemento e os filhos (UiRect::visible; cria o UiRect se faltar).
    bool SetVisible(std::string_view entity, bool visible);
    bool SetVisible(EntityId entity, bool visible);
    bool IsVisible(std::string_view entity) const;  // visível de fato: ativa, ela e os pais com visible
    // O botão foi clicado (soltou o clique nele no frame anterior). Leia no OnUpdate.
    bool WasClicked(std::string_view entity) const;
    bool WasClicked(EntityId entity) const;
    EntityId HoveredButton() const { return m_hovered; }  // botão sob o mouse, ou 0 (num documento, a entidade dele)
    // Retângulo do elemento na tela no último OnUpdate (pixels). false se não estava visível.
    bool ScreenRect(EntityId entity, UiBox& out) const;
    bool ScreenRect(const UiElement& element, UiBox& out) const;

    // Elemento de documento com esse nome (o primeiro, entre as entidades com UiDocument). nullptr se não houver.
    UiElement* FindElement(std::string_view name) const;

private:
    Entity* FindEntity(std::string_view name) const;
    Entity* FindEntity(EntityId id) const;
    void UpdateElementStates(Scene& scene, bool down, const UiElement* clicked);

    SceneRuntime* m_runtime = nullptr;
    UiPointer m_pointer;
    std::vector<UiNode> m_layout;  // do último OnUpdate
    EntityId m_hovered = 0;
    EntityId m_pressed = 0;        // botão onde o clique começou
    const UiElement* m_hoveredElement = nullptr;  // num documento: só comparados, nunca lidos (a árvore pode mudar)
    const UiElement* m_pressedElement = nullptr;
    bool m_wasDown = false;
    bool m_captured = false;       // o clique começou sobre a interface: é dela até soltar
    bool m_captureWasDown = false;
};

} // namespace motor
