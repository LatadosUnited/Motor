#include "motor/ui/UiSystem.h"

#include "motor/core/Input.h"
#include "motor/core/Log.h"
#include "motor/project/InputActions.h"
#include "motor/scene/GameHost.h"
#include "motor/scene/Scene.h"
#include "motor/scene/SceneRuntime.h"
#include "motor/ui/UiDocument.h"

#include "UiFlexLayout.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <exception>

namespace motor {

MOTOR_RUNTIME_SYSTEM(Ui, UiSystem, kSystemOrderUi);

namespace {
// A parte da UI que roda antes dos scripts (um sistema tem uma ordem só; ver RuntimeSystem.h).
class UiPointerSystem final : public RuntimeSystem {
public:
    void OnUpdate(SceneRuntime& runtime, float) override {
        if (UiSystem* ui = runtime.GetSystem<UiSystem>()) ui->UpdatePointer(runtime);
    }
};

constexpr float kNavFirstRepeat = 0.4f;  // segurar a direção: espera isto e depois anda a cada kNavRepeat
constexpr float kNavRepeat = 0.12f;
constexpr float kNavThreshold = 0.5f;    // analógico além disto conta como direção

const char* EventName(UiEventType type) {
    switch (type) {
        case UiEventType::Click: return "OnClick";
        case UiEventType::Change: return "OnChange";
        case UiEventType::Hover: return "OnHover";
    }
    return "evento";
}

// UTF-8: começo do caractere antes/depois de `i` (bytes de continuação são 10xxxxxx).
size_t PrevChar(const std::string& s, size_t i) {
    if (i == 0) return 0;
    --i;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    return i;
}
size_t NextChar(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    ++i;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    return i;
}
size_t CountChars(std::string_view s) {
    return static_cast<size_t>(std::count_if(s.begin(), s.end(), [](char c) {
        return (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    }));
}

// Direção dominante de um vetor de navegação (Y para cima +), em coordenadas da tela (Y para baixo); zero se fraco.
glm::vec2 NavDirection(glm::vec2 v) {
    if (std::max(std::abs(v.x), std::abs(v.y)) < kNavThreshold) return glm::vec2(0.0f);
    if (std::abs(v.x) >= std::abs(v.y)) return {v.x > 0.0f ? 1.0f : -1.0f, 0.0f};
    return {0.0f, v.y > 0.0f ? -1.0f : 1.0f};
}

// A ação do mapa do projeto, ou a padrão (projeto antigo sem Navegar/Confirmar/Voltar).
const InputActions& DefaultActions() {
    static const InputActions defaults = InputActions::Defaults();
    return defaults;
}
const InputActions& ActionsWith(const InputActions& project, std::string_view action) {
    return project.Find(action) ? project : DefaultActions();
}
} // namespace
MOTOR_RUNTIME_SYSTEM(UiPonteiro, UiPointerSystem, kSystemOrderUiInput);

void UiSystem::OnStart(SceneRuntime& runtime) {
    m_runtime = &runtime;
    m_layout.clear();
    m_hovered = m_pressed = 0;
    m_hoveredElement = m_pressedElement = nullptr;
    m_wasDown = false;
    m_captured = m_captureWasDown = false;
    m_clicked.clear();
    m_events.clear();
    m_navDir = glm::vec2(0.0f);
    m_navRepeat = 0.0f;
}

void UiSystem::UpdatePointer(SceneRuntime& runtime) {
    Scene& scene = runtime.GetScene();
    // Calculado de novo: a árvore de um documento pode ter sido refeita desde o último passo (os nós apontam para ela).
    m_layout = ComputeUiLayout(scene, m_pointer.screenSize);
    const bool locked = runtime.GetCursorMode() == CursorMode::Locked;
    const bool over = m_pointer.inside && !locked && UiHitTest(scene, m_layout, m_pointer.position) != kNullEntity;
    const bool down = m_pointer.down;
    if (down && !m_captureWasDown) m_captured = over;  // decide no aperto e vale até soltar
    if (!down) m_captured = false;
    m_captureWasDown = down;
    runtime.SetUiPointer(over, m_captured);
}

void UiSystem::OnStop(SceneRuntime&) {
    // Os callbacks são código da DLL de scripts: soltos aqui, com ela ainda carregada (a cena do Play pode durar mais).
    ClearAllHandlers();
    m_events.clear();
    m_runtime = nullptr;
    m_layout.clear();
}

void UiSystem::OnScriptsForgotten(SceneRuntime&) {
    ClearAllHandlers();
    m_events.clear();
}

void UiSystem::ForEachTree(const std::function<void(std::vector<UiElement>&)>& fn) const {
    if (!m_runtime) return;
    Scene& scene = m_runtime->GetScene();
    for (EntityId id : scene.EntitiesWith<UiDocument>()) {
        const Entity* e = scene.Find(id);
        const UiDocument* document = e ? e->Get<UiDocument>() : nullptr;
        if (UiDocumentData* tree = document ? document->Tree() : nullptr) fn(tree->elements);
    }
}

void UiSystem::ClearAllHandlers() {
    struct Walk {
        static void Clear(std::vector<UiElement>& list) {
            for (UiElement& e : list) {
                e.handlers.clear();
                Clear(e.children);
            }
        }
    };
    ForEachTree([](std::vector<UiElement>& list) { Walk::Clear(list); });
}

bool UiSystem::IsLive(const UiElement* element) const {
    if (!element) return false;
    for (const UiNode& node : m_layout) {
        if (node.element == element) return true;  // o caso comum: no layout deste passo
    }
    bool found = false;
    struct Walk {
        static bool Has(const std::vector<UiElement>& list, const UiElement* target) {
            for (const UiElement& e : list) {
                if (&e == target || Has(e.children, target)) return true;
            }
            return false;
        }
    };
    ForEachTree([&](std::vector<UiElement>& list) { found = found || Walk::Has(list, element); });
    return found;
}

void UiSystem::OnUpdate(SceneRuntime& runtime, float dt) {
    Scene& scene = runtime.GetScene();
    m_layout = ComputeUiLayout(scene, m_pointer.screenSize);
    m_clicked.clear();

    // Controle sob o mouse: o elemento de cima que bloqueia o clique, se for um controle interativo.
    EntityId hovered = 0;
    UiElement* hoveredElement = nullptr;
    if (m_pointer.inside) {
        if (const UiNode* top = UiHitTestNode(scene, m_layout, m_pointer.position)) {
            if (UiElement* element = top->element) {
                if (element->IsFocusable()) {
                    hovered = top->entity;
                    hoveredElement = element;
                }
            } else {
                const Entity* e = scene.Find(top->entity);
                const UiButton* button = e ? e->Get<UiButton>() : nullptr;
                if (button && button->interactable) hovered = top->entity;
            }
        }
    }
    const bool down = m_pointer.down;
    const bool pressedNow = down && !m_wasDown;
    const bool released = !down && m_wasDown;
    EntityId clicked = 0;
    if (pressedNow) {  // clique que começa fora de um botão não vale para nenhum
        m_pressed = hovered;
        m_pressedElement = hoveredElement;
        // O campo de texto ganha o foco no clique; clicar em outro lugar tira.
        if (hoveredElement && hoveredElement->type == UiElementType::TextField) {
            Focus(hoveredElement);
        } else if (UiElement* focused = Focused(); focused && focused->type == UiElementType::TextField) {
            ClearFocus();
        }
    }
    // Slider arrastado: o valor segue o mouse enquanto o botão está apertado (só se o elemento ainda está na tela).
    if (down && m_pressedElement && m_pressedElement->type == UiElementType::Slider) {
        for (const UiNode& node : m_layout) {
            if (node.element != m_pressedElement) continue;
            UiElement& slider = *node.element;
            if (!slider.interactable) break;
            float x0 = 0.0f, x1 = 0.0f;
            UiSliderTrack(node.content, x0, x1);
            const float t = x1 > x0 ? std::clamp((m_pointer.position.x - x0) / (x1 - x0), 0.0f, 1.0f) : 0.0f;
            const float lo = std::min(slider.minValue, slider.maxValue), hi = std::max(slider.minValue, slider.maxValue);
            if (slider.SetValue(lo + (hi - lo) * t)) Emit(slider, UiEventType::Change);
            break;
        }
    }
    if (released) {
        if (m_pressed && m_pressed == hovered && m_pressedElement == hoveredElement) {
            if (hoveredElement) Activate(*hoveredElement);  // hoveredElement é deste passo: vivo
            else clicked = m_pressed;
        }
        m_pressed = 0;
        m_pressedElement = nullptr;
    }
    m_wasDown = down;
    m_hovered = hovered;
    m_hoveredElement = hoveredElement;

    HandleNavigation(runtime, dt);
    UpdateElementStates(down);
    // Estilos dos estados novos (:hover, :active, :focus) já neste passo, e as transições andam com o tempo do jogo.
    for (EntityId id : scene.EntitiesWith<UiDocument>()) {
        const Entity* e = scene.Find(id);
        const UiDocument* document = e ? e->Get<UiDocument>() : nullptr;
        if (UiDocumentData* tree = document ? document->Tree() : nullptr) {
            tree->animate = true;
            UpdateUiStyles(*tree);
            AdvanceUiTransitions(*tree, dt);
        }
    }

    // Roda do mouse: rola a lista (overflow Scroll) de cima que está sob o mouse e ainda tem para onde ir.
    const float wheel = m_pointer.inside ? Input::ScrollDelta() : 0.0f;
    if (wheel != 0.0f) {
        for (auto it = m_layout.rbegin(); it != m_layout.rend(); ++it) {
            UiElement* e = it->element;
            if (!e || e->overflow != UiOverflow::Scroll || e->scrollMax <= 0.0f) continue;
            if (!it->box.Contains(m_pointer.position) || (it->clipped && !it->clip.Contains(m_pointer.position))) continue;
            e->scroll = std::clamp(e->scroll - wheel * kUiScrollStep, 0.0f, e->scrollMax);  // para cima = positivo
            break;
        }
    }

    // O estado vai nos componentes (o editor mostra a cor certa; os scripts leem com WasClicked).
    for (EntityId id : scene.EntitiesWith<UiButton>()) {
        Entity* e = scene.Find(id);
        UiButton* button = e ? e->Get<UiButton>() : nullptr;
        if (!button) continue;
        const bool self = m_hoveredElement == nullptr;  // o botão sob o mouse é a entidade, não um elemento dela
        button->hovered = self && id == hovered;
        button->pressed = self && m_pressedElement == nullptr && id == m_pressed && down && id == hovered;
        button->clicked = id == clicked;
    }

    DispatchEvents();
}

void UiSystem::Activate(UiElement& element) {
    // O campo de texto só ganha o foco no clique (o OnClick dele é o Enter, "enviar").
    if (!element.interactable || element.type == UiElementType::TextField) return;
    if (element.type == UiElementType::Toggle) {
        element.checked = !element.checked;
        Emit(element, UiEventType::Change);
    }
    m_clicked.push_back(&element);
    Emit(element, UiEventType::Click);
}

void UiSystem::Emit(UiElement& element, UiEventType type, bool flag) {
    const bool wanted = std::any_of(element.handlers.begin(), element.handlers.end(),
                                    [&](const UiEventHandler& h) { return h.type == type; });
    if (wanted) m_events.push_back({&element, type, flag});
}

void UiSystem::DispatchEvents() {
    // Fila copiada: um callback pode causar outro evento (registrar, focar) que fica para o próximo passo.
    const std::vector<PendingEvent> events = std::exchange(m_events, {});
    for (const PendingEvent& ev : events) {
        if (!IsLive(ev.element)) continue;  // um callback anterior trocou o documento
        // Cópia dos callbacks: este pode registrar ou tirar outros do mesmo elemento.
        std::vector<UiEventHandler> handlers;
        for (const UiEventHandler& h : ev.element->handlers) {
            if (h.type == ev.type) handlers.push_back(h);
        }
        std::vector<uint32_t> dead;
        for (const UiEventHandler& h : handlers) {
            if (!IsLive(ev.element)) break;
            UiElement& element = *ev.element;
            const auto call = [&]() { h.fn(element, ev.flag); };
            if (h.owner && m_runtime) {
                if (!m_runtime->CallScriptCallback(h.owner, h.ownerEntity, EventName(ev.type), call)) dead.push_back(h.id);
            } else {
                try {
                    call();
                } catch (const std::exception& ex) {
                    log::Error("UI: {} de {}: {}", EventName(ev.type), element.name, ex.what());
                } catch (...) {
                    log::Error("UI: {} de {}: exceção desconhecida", EventName(ev.type), element.name);
                }
            }
        }
        if (!dead.empty() && IsLive(ev.element)) {  // o script dono sumiu: o callback sai junto
            std::erase_if(ev.element->handlers, [&](const UiEventHandler& h) {
                return std::find(dead.begin(), dead.end(), h.id) != dead.end();
            });
        }
    }
}

void UiSystem::UpdateElementStates(std::vector<UiElement>& elements, bool down) {
    for (UiElement& e : elements) {
        const bool hovered = &e == m_hoveredElement;
        if (hovered != e.hovered) Emit(e, UiEventType::Hover, hovered);  // entrou ou saiu
        e.hovered = hovered;
        e.pressed = hovered && down && &e == m_pressedElement;
        e.clicked = std::find(m_clicked.begin(), m_clicked.end(), &e) != m_clicked.end();
        UpdateElementStates(e.children, down);
    }
}

void UiSystem::UpdateElementStates(bool down) {
    ForEachTree([&](std::vector<UiElement>& list) { UpdateElementStates(list, down); });
}

// --- Foco e navegação ---------------------------------------------------------------------------------------------

UiElement* UiSystem::Focused() const {
    UiElement* found = nullptr;
    struct Walk {
        static UiElement* Find(std::vector<UiElement>& list) {
            for (UiElement& e : list) {
                if (e.focused) return &e;
                if (UiElement* c = Find(e.children)) return c;
            }
            return nullptr;
        }
    };
    ForEachTree([&](std::vector<UiElement>& list) {
        if (!found) found = Walk::Find(list);
    });
    return found;
}

void UiSystem::ClearFocus() {
    struct Walk {
        static void Clear(std::vector<UiElement>& list) {
            for (UiElement& e : list) {
                e.focused = false;
                Clear(e.children);
            }
        }
    };
    ForEachTree([](std::vector<UiElement>& list) { Walk::Clear(list); });
}

bool UiSystem::Focus(UiElement* element) {
    if (!element || !element->IsFocusable()) return false;
    if (element->focused) return true;
    ClearFocus();
    element->focused = true;
    if (element->type == UiElementType::TextField) element->caret = element->text.size();  // digita no fim
    return true;
}

bool UiSystem::Focus(std::string_view selector) {
    for (UiElement* e : Query(selector)) {
        if (Focus(e)) return true;
    }
    return false;
}

void UiSystem::MoveFocus(glm::vec2 direction) {
    // O controle visível mais perto na direção: distância ao longo dela mais o dobro da distância de lado (o que está
    // alinhado ganha do que está mais perto mas na diagonal).
    UiElement* focused = Focused();
    const UiNode* from = nullptr;
    for (const UiNode& n : m_layout) {
        if (n.element && n.element == focused) from = &n;
    }
    if (!from) {
        StepFocus(1);  // nada em foco (ou fora da tela): o primeiro
        return;
    }
    const glm::vec2 origin = from->box.Center();
    const UiNode* best = nullptr;
    float bestScore = 0.0f;
    for (const UiNode& n : m_layout) {
        if (!n.element || n.element == focused || !n.element->IsFocusable() || n.box.Size().x <= 0.0f) continue;
        const glm::vec2 d = n.box.Center() - origin;
        const float along = glm::dot(d, direction);
        if (along <= 1.0f) continue;
        const float side = std::abs(d.x * direction.y - d.y * direction.x);
        const float score = along + 2.0f * side;
        if (!best || score < bestScore) {
            best = &n;
            bestScore = score;
        }
    }
    if (best) Focus(best->element);
}

void UiSystem::StepFocus(int delta) {
    std::vector<UiElement*> order;  // a ordem do documento (a do layout), só os visíveis
    for (const UiNode& n : m_layout) {
        if (n.element && n.element->IsFocusable() && n.box.Size().x > 0.0f) order.push_back(n.element);
    }
    if (order.empty()) return;
    const UiElement* focused = Focused();
    const auto it = std::find(order.begin(), order.end(), focused);
    int index = 0;
    if (it != order.end()) {
        const int n = static_cast<int>(order.size());
        index = ((static_cast<int>(it - order.begin()) + delta) % n + n) % n;  // dá a volta
    } else if (delta < 0) {
        index = static_cast<int>(order.size()) - 1;
    }
    Focus(order[static_cast<size_t>(index)]);
}

void UiSystem::EditText(UiElement& field) {
    std::string& text = field.text;
    field.caret = std::min(field.caret, text.size());
    bool changed = false;
    const std::string& typed = Input::TextInput();
    if (!typed.empty()) {
        std::string insert = typed;
        if (field.maxLength > 0) {
            const size_t room = static_cast<size_t>(field.maxLength) - std::min<size_t>(CountChars(text), field.maxLength);
            size_t end = 0, chars = 0;
            while (end < insert.size() && chars < room) {
                end = NextChar(insert, end);
                ++chars;
            }
            insert.resize(end);
        }
        if (!insert.empty()) {
            text.insert(field.caret, insert);
            field.caret += insert.size();
            changed = true;
        }
    }
    if (Input::WasKeyPressed(Key::Backspace) && field.caret > 0) {
        const size_t start = PrevChar(text, field.caret);
        text.erase(start, field.caret - start);
        field.caret = start;
        changed = true;
    }
    if (Input::WasKeyPressed(Key::Delete) && field.caret < text.size()) {
        text.erase(field.caret, NextChar(text, field.caret) - field.caret);
        changed = true;
    }
    if (Input::WasKeyPressed(Key::Left)) field.caret = PrevChar(text, field.caret);
    if (Input::WasKeyPressed(Key::Right)) field.caret = NextChar(text, field.caret);
    if (Input::WasKeyPressed(Key::Home)) field.caret = 0;
    if (Input::WasKeyPressed(Key::End)) field.caret = text.size();
    if (changed) Emit(field, UiEventType::Change);
    if (Input::WasKeyPressed(Key::Enter) || Input::WasKeyPressed(Key::NumpadEnter)) {
        m_clicked.push_back(&field);
        Emit(field, UiEventType::Click);  // enviar
    }
}

void UiSystem::HandleNavigation(SceneRuntime& runtime, float dt) {
    UiElement* focused = Focused();
    if (focused) {  // escondido ou não pode mais ter foco: perde
        const bool shown = std::any_of(m_layout.begin(), m_layout.end(), [&](const UiNode& n) { return n.element == focused; });
        if (!shown || !focused->IsFocusable()) {
            ClearFocus();
            focused = nullptr;
        }
    }
    if (!runtime.InputEnabled()) {  // o jogo sem foco (digitando no editor)
        m_navDir = glm::vec2(0.0f);
        return;
    }
    const InputActions& project = runtime.Host() ? runtime.Host()->Actions() : DefaultActions();
    InputFilter filter;
    filter.contexts = &runtime.InputContexts();
    const InputActions& nav = ActionsWith(project, "Navegar");
    const InputAction* navAction = nav.Find("Navegar");
    const bool menu = navAction && InputActions::IsContextActive(*navAction, filter);

    const bool editing = focused && focused->type == UiElementType::TextField;
    if (editing) {
        EditText(*focused);  // digitar vale fora do contexto Menu também (o clique deu o foco)
        if (ActionsWith(project, "Voltar").WasPressed("Voltar", filter) || Input::WasKeyPressed(Key::Escape)) {
            ClearFocus();
            m_navDir = glm::vec2(0.0f);
            return;
        }
    }
    if (!menu) {
        m_navDir = glm::vec2(0.0f);
        return;
    }

    // Tab e Shift+Tab: o próximo e o anterior na ordem do documento.
    if (Input::WasKeyPressed(Key::Tab)) {
        StepFocus(Input::IsKeyDown(Key::LeftShift) || Input::IsKeyDown(Key::RightShift) ? -1 : 1);
        focused = Focused();
    }
    // Direção: a ação Navegar; digitando num campo, só as setas de cima e de baixo (WASD são letras).
    glm::vec2 dir{0.0f};
    if (editing) {
        if (Input::IsKeyDown(Key::Up)) dir = {0.0f, -1.0f};
        else if (Input::IsKeyDown(Key::Down)) dir = {0.0f, 1.0f};
    } else {
        dir = NavDirection(nav.Vector("Navegar", filter));
    }
    bool step = false;
    if (dir != m_navDir) {
        step = dir != glm::vec2(0.0f);
        m_navRepeat = kNavFirstRepeat;
    } else if (dir != glm::vec2(0.0f)) {
        m_navRepeat -= dt;
        if (m_navRepeat <= 0.0f) {
            step = true;
            m_navRepeat = kNavRepeat;
        }
    }
    m_navDir = dir;
    if (step) {
        if (focused && focused->type == UiElementType::Slider && dir.y == 0.0f) {
            const float range = std::abs(focused->maxValue - focused->minValue);
            const float amount = focused->step > 0.0f ? focused->step : range * 0.05f;
            if (focused->SetValue(focused->value + dir.x * amount)) Emit(*focused, UiEventType::Change);
        } else {
            MoveFocus(dir);
            focused = Focused();
        }
    }
    if (focused && !editing && ActionsWith(project, "Confirmar").WasPressed("Confirmar", filter)) Activate(*focused);
}

// --- Consultas e eventos ------------------------------------------------------------------------------------------

std::vector<UiElement*> UiSystem::Query(std::string_view selector) const {
    std::vector<UiElement*> out;
    std::vector<UiSelector> selectors;
    std::string error;
    if (!ParseUiQuery(selector, selectors, &error)) {
        log::Warn("UI: consulta \"{}\": {}", selector, error);
        return out;
    }
    struct Walk {
        static void Collect(std::vector<UiElement>& list, const std::vector<UiSelector>& s, std::vector<UiElement*>& out) {
            for (UiElement& e : list) {
                if (UiQueryMatches(s, e)) out.push_back(&e);
                Collect(e.children, s, out);
            }
        }
    };
    ForEachTree([&](std::vector<UiElement>& list) { Walk::Collect(list, selectors, out); });
    return out;
}

UiElement* UiSystem::Q(std::string_view selector) const {
    const std::vector<UiElement*> found = Query(selector);
    return found.empty() ? nullptr : found.front();
}

bool UiSystem::Register(UiElement* element, UiEventType type, std::function<void(UiElement&, bool)> fn) {
    if (!element || !fn) return false;
    UiEventHandler handler;
    handler.type = type;
    handler.id = m_nextHandler++;
    if (m_runtime) {  // o dono é o script que está rodando (no OnStart dele, normalmente)
        handler.owner = m_runtime->RunningScript();
        handler.ownerEntity = m_runtime->RunningScriptEntity();
    }
    handler.fn = std::move(fn);
    element->handlers.push_back(std::move(handler));
    return true;
}

int UiSystem::Register(std::string_view selector, UiEventType type, std::function<void(UiElement&, bool)> fn) {
    const std::vector<UiElement*> elements = Query(selector);
    if (elements.empty()) log::Warn("UI: {}(\"{}\"): nenhum elemento casa com o seletor", EventName(type), selector);
    for (UiElement* e : elements) Register(e, type, fn);
    return static_cast<int>(elements.size());
}

int UiSystem::OnClick(std::string_view selector, std::function<void()> fn) {
    if (!fn) return 0;
    return Register(selector, UiEventType::Click, [fn = std::move(fn)](UiElement&, bool) { fn(); });
}
int UiSystem::OnClick(std::string_view selector, std::function<void(UiElement&)> fn) {
    if (!fn) return 0;
    return Register(selector, UiEventType::Click, [fn = std::move(fn)](UiElement& e, bool) { fn(e); });
}
int UiSystem::OnChange(std::string_view selector, std::function<void(UiElement&)> fn) {
    if (!fn) return 0;
    return Register(selector, UiEventType::Change, [fn = std::move(fn)](UiElement& e, bool) { fn(e); });
}
int UiSystem::OnHover(std::string_view selector, std::function<void(UiElement&, bool)> fn) {
    return Register(selector, UiEventType::Hover, std::move(fn));
}
bool UiSystem::OnClick(UiElement* element, std::function<void()> fn) {
    if (!fn) return false;
    return Register(element, UiEventType::Click, [fn = std::move(fn)](UiElement&, bool) { fn(); });
}
bool UiSystem::OnClick(UiElement* element, std::function<void(UiElement&)> fn) {
    if (!fn) return false;
    return Register(element, UiEventType::Click, [fn = std::move(fn)](UiElement& e, bool) { fn(e); });
}
bool UiSystem::OnChange(UiElement* element, std::function<void(UiElement&)> fn) {
    if (!fn) return false;
    return Register(element, UiEventType::Change, [fn = std::move(fn)](UiElement& e, bool) { fn(e); });
}
bool UiSystem::OnHover(UiElement* element, std::function<void(UiElement&, bool)> fn) {
    return Register(element, UiEventType::Hover, std::move(fn));
}

int UiSystem::ClearEvents(std::string_view selector) {
    int removed = 0;
    for (UiElement* e : Query(selector)) {
        removed += static_cast<int>(e->handlers.size());
        e->handlers.clear();
    }
    return removed;
}

// --- Por nome -----------------------------------------------------------------------------------------------------

UiElement* UiSystem::FindElement(std::string_view name) const {
    if (!m_runtime || name.empty()) return nullptr;
    Scene& scene = m_runtime->GetScene();
    for (EntityId id : scene.EntitiesWith<UiDocument>()) {
        const Entity* e = scene.Find(id);
        const UiDocument* document = e ? e->Get<UiDocument>() : nullptr;
        UiDocumentData* tree = document ? document->Tree() : nullptr;
        if (UiElement* element = tree ? tree->Find(name) : nullptr) return element;
    }
    return nullptr;
}

Entity* UiSystem::FindEntity(std::string_view name) const {
    return m_runtime ? m_runtime->GetScene().FindByName(name) : nullptr;
}

Entity* UiSystem::FindEntity(EntityId id) const {
    return m_runtime ? m_runtime->GetScene().Find(id) : nullptr;
}

bool UiSystem::SetText(EntityId entity, std::string_view text) {
    Entity* e = FindEntity(entity);
    UiText* ui = e ? e->Get<UiText>() : nullptr;
    if (!ui) return false;
    if (ui->text != text) ui->text = std::string(text);  // mesmo texto: nada muda (a geometria fica em cache)
    return true;
}

bool UiSystem::SetText(std::string_view entity, std::string_view text) {
    if (const Entity* e = FindEntity(entity)) return SetText(e->id, text);
    UiElement* element = FindElement(entity);
    if (!element) return false;
    if (element->text != text) {
        element->text = std::string(text);
        element->caret = std::min(element->caret, element->text.size());
    }
    return true;
}

std::string UiSystem::GetText(std::string_view entity) const {
    if (const Entity* e = FindEntity(entity)) {
        const UiText* ui = e->Get<UiText>();
        return ui ? ui->text : std::string();
    }
    const UiElement* element = FindElement(entity);
    return element ? element->text : std::string();
}

bool UiSystem::SetVisible(EntityId entity, bool visible) {
    Entity* e = FindEntity(entity);
    if (!e) return false;
    UiRect* rect = e->Get<UiRect>();
    if (!rect) rect = &e->Add<UiRect>();
    rect->visible = visible;
    return true;
}

bool UiSystem::SetVisible(std::string_view entity, bool visible) {
    if (const Entity* e = FindEntity(entity)) return SetVisible(e->id, visible);
    UiElement* element = FindElement(entity);
    if (!element) return false;
    element->visible = visible;
    return true;
}

bool UiSystem::IsVisible(std::string_view entity) const {
    if (!m_runtime) return false;
    const Entity* e = FindEntity(entity);
    if (!e) {
        // Elemento de documento: visível se apareceu no último layout (ele, os pais e a entidade do documento).
        const UiElement* element = FindElement(entity);
        UiBox box;
        return element && ScreenRect(*element, box);
    }
    const Scene& scene = m_runtime->GetScene();
    if (!scene.IsActiveInHierarchy(e->id)) return false;
    for (const Entity* p = e; p; p = scene.Find(p->parent)) {
        const UiRect* rect = p->Get<UiRect>();
        if (rect && !rect->visible) return false;
    }
    return true;
}

bool UiSystem::WasClicked(EntityId entity) const {
    const Entity* e = FindEntity(entity);
    const UiButton* button = e ? e->Get<UiButton>() : nullptr;
    return button && button->clicked;
}

bool UiSystem::WasClicked(std::string_view entity) const {
    if (const Entity* e = FindEntity(entity)) return WasClicked(e->id);
    const UiElement* element = FindElement(entity);
    return element && element->clicked;
}

bool UiSystem::ScreenRect(const UiElement& element, UiBox& out) const {
    for (const UiNode& node : m_layout) {
        if (node.element == &element) {
            out = node.box;
            return true;
        }
    }
    return false;
}

bool UiSystem::ScreenRect(EntityId entity, UiBox& out) const {
    for (const UiNode& node : m_layout) {
        if (node.entity == entity && !node.element) {
            out = node.box;
            return true;
        }
    }
    return false;
}

} // namespace motor
