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
// Estilos (UiStyle.h): o botão sob o mouse fica com :hover e o apertado com :active; a cascata roda de novo só quando um
// estado muda, e as transições (`transition`) andam com o dt de cada passo.
//
// Consultas e eventos (Etapa 6): Q("#jogar") e Query(".item") acham elementos dos documentos por seletor (o mesmo das
// folhas: tipo, .classe, #nome, :estado); OnClick, OnChange e OnHover registram callbacks, chamados depois dos scripts
// no passo em que a coisa aconteceu:
//   ui->OnClick("#jogar", [this] { LoadScene("Fase1"); });
//   ui->OnChange("#volume", [this](motor::UiElement& e) { volume = e.value; });
// O callback é do script que o registrou (o que roda na hora do OnClick): some quando o script ou a entidade somem,
// ao parar o Play e na recarga da DLL de scripts (OnScriptsForgotten, antes de a DLL antiga sair da memória). A
// árvore relida do arquivo (Recarregar, .mui mudado) também começa sem eles: registre de novo no OnStart.
//
// Controles: o Slider anda com o mouse arrastado; o Toggle troca no clique; o TextField ganha o foco no clique e
// recebe o texto digitado (Input::TextInput), Backspace, Delete, setas, Home e End; Enter dá OnClick (enviar) e Esc tira o
// foco. Cada mudança de valor dá OnChange.
//
// Foco (contexto de entrada Menu, ver Script::SetInputContext): a ação Navegar (setas, WASD, direcional, analógico)
// move o foco para o controle mais perto naquela direção (Tab e Shift+Tab: o próximo e o anterior na ordem do
// documento); no Slider em foco, esquerda e direita mudam o valor. Confirmar (Enter, Espaço, A) clica o botão em foco e
// troca o Toggle. O elemento em foco fica com :focus (a folha padrão põe uma borda amarela).
//
// Clique da interface não vai para o jogo: antes dos scripts (UiPointerSystem), a UI diz ao runtime se o mouse
// está sobre um botão ou imagem e se o clique começou num deles; até soltar, o botão esquerdo fica solto para
// os scripts (IsMouseDown, ações). Com o cursor preso (Locked), a interface não pega o mouse.

#include "motor/scene/RuntimeSystem.h"
#include "motor/ui/UiLayout.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

class InputActions;
class Scene;
struct Entity;
struct UiElement;
enum class UiEventType : uint8_t;

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
    void OnScriptsForgotten(SceneRuntime& runtime) override;  // recarga da DLL de scripts: solta os callbacks
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

    // --- Documentos: consultas, eventos e foco ---
    // Elementos que casam com o seletor ("#jogar", ".item", "Button.menu", "Slider:focus"; um nome sozinho também acha
    // pelo nome), nos documentos da cena (na ordem da cena; dentro de cada um, em profundidade). Q: o primeiro, ou
    // nullptr. Seletor inválido: nenhum, com aviso no log.
    UiElement* Q(std::string_view selector) const;
    std::vector<UiElement*> Query(std::string_view selector) const;
    // Registra o callback em todos os elementos que casam agora; devolve quantos (0: nenhum, com aviso no log).
    int OnClick(std::string_view selector, std::function<void()> fn);
    int OnClick(std::string_view selector, std::function<void(UiElement&)> fn);
    int OnChange(std::string_view selector, std::function<void(UiElement&)> fn);  // value, checked ou text mudou
    int OnHover(std::string_view selector, std::function<void(UiElement&, bool entered)> fn);
    // O mesmo num elemento já achado (Q). false se `element` for nulo.
    bool OnClick(UiElement* element, std::function<void()> fn);
    bool OnClick(UiElement* element, std::function<void(UiElement&)> fn);
    bool OnChange(UiElement* element, std::function<void(UiElement&)> fn);
    bool OnHover(UiElement* element, std::function<void(UiElement&, bool entered)> fn);
    int ClearEvents(std::string_view selector);  // tira os callbacks dos elementos; devolve quantos tirou
    // Foco: o controle (Button, Slider, Toggle, TextField interativo) que recebe a navegação e o Confirmar.
    bool Focus(std::string_view selector);  // o primeiro que casa e pode ter foco; false se nenhum
    bool Focus(UiElement* element);         // false se nulo ou não pode ter foco
    UiElement* Focused() const;             // o elemento em foco, ou nullptr
    void ClearFocus();

private:
    struct PendingEvent {
        UiElement* element = nullptr;
        UiEventType type{};
        bool flag = false;  // Hover: entrou
    };
    int Register(std::string_view selector, UiEventType type, std::function<void(UiElement&, bool)> fn);
    bool Register(UiElement* element, UiEventType type, std::function<void(UiElement&, bool)> fn);
    void Emit(UiElement& element, UiEventType type, bool flag = false);
    void Activate(UiElement& element);  // clique do mouse ou Confirmar: troca o Toggle e emite Click (e Change)
    void HandleNavigation(SceneRuntime& runtime, float dt);
    void EditText(UiElement& field);
    void MoveFocus(glm::vec2 direction);
    void StepFocus(int delta);  // Tab
    void DispatchEvents();
    bool IsLive(const UiElement* element) const;  // ainda está numa árvore viva da cena (o callback pode ter mudado)
    void ForEachTree(const std::function<void(std::vector<UiElement>&)>& fn) const;
    void ClearAllHandlers();

    Entity* FindEntity(std::string_view name) const;
    Entity* FindEntity(EntityId id) const;
    void UpdateElementStates(bool down);
    void UpdateElementStates(std::vector<UiElement>& elements, bool down);

    SceneRuntime* m_runtime = nullptr;
    UiPointer m_pointer;
    std::vector<UiNode> m_layout;  // do último OnUpdate
    EntityId m_hovered = 0;
    EntityId m_pressed = 0;        // botão onde o clique começou
    // Num documento: entre passos só comparados com os nós do layout novo (a árvore pode ter sido refeita), nunca lidos.
    UiElement* m_hoveredElement = nullptr;
    UiElement* m_pressedElement = nullptr;
    std::vector<UiElement*> m_clicked;  // clicados neste passo (mouse ou Confirmar)
    std::vector<PendingEvent> m_events;
    uint32_t m_nextHandler = 1;
    glm::vec2 m_navDir{0.0f};  // direção da navegação segurada (repete depois de um tempo)
    float m_navRepeat = 0.0f;
    bool m_wasDown = false;
    bool m_captured = false;       // o clique começou sobre a interface: é dela até soltar
    bool m_captureWasDown = false;
};

} // namespace motor
