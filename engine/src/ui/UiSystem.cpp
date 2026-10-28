#include "motor/ui/UiSystem.h"

#include "motor/core/Input.h"
#include "motor/scene/Scene.h"
#include "motor/scene/SceneRuntime.h"
#include "motor/ui/UiDocument.h"

#include <algorithm>

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
} // namespace
MOTOR_RUNTIME_SYSTEM(UiPonteiro, UiPointerSystem, kSystemOrderUiInput);

void UiSystem::OnStart(SceneRuntime& runtime) {
    m_runtime = &runtime;
    m_layout.clear();
    m_hovered = m_pressed = 0;
    m_hoveredElement = m_pressedElement = nullptr;
    m_wasDown = false;
    m_captured = m_captureWasDown = false;
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
    m_runtime = nullptr;
    m_layout.clear();
}

void UiSystem::OnUpdate(SceneRuntime& runtime, float) {
    Scene& scene = runtime.GetScene();
    m_layout = ComputeUiLayout(scene, m_pointer.screenSize);

    // Botão sob o mouse: o elemento de cima que bloqueia o clique, se for um botão clicável.
    EntityId hovered = 0;
    const UiElement* hoveredElement = nullptr;
    if (m_pointer.inside) {
        if (const UiNode* top = UiHitTestNode(scene, m_layout, m_pointer.position)) {
            if (const UiElement* element = top->element) {
                if (element->type == UiElementType::Button && element->interactable) {
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
    const UiElement* clickedElement = nullptr;
    if (pressedNow) {  // clique que começa fora de um botão não vale para nenhum
        m_pressed = hovered;
        m_pressedElement = hoveredElement;
    }
    if (released) {
        if (m_pressed && m_pressed == hovered && m_pressedElement == hoveredElement) {
            if (hoveredElement) clickedElement = hoveredElement;
            else clicked = m_pressed;
        }
        m_pressed = 0;
        m_pressedElement = nullptr;
    }
    m_wasDown = down;
    m_hovered = hovered;
    m_hoveredElement = hoveredElement;
    UpdateElementStates(scene, down, clickedElement);

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
}

void UiSystem::UpdateElementStates(Scene& scene, bool down, const UiElement* clicked) {
    struct Walker {
        const UiSystem& ui;
        bool down;
        const UiElement* clicked;
        void Walk(std::vector<UiElement>& elements) const {
            for (UiElement& e : elements) {
                e.hovered = &e == ui.m_hoveredElement;
                e.pressed = e.hovered && down && &e == ui.m_pressedElement;
                e.clicked = &e == clicked;
                Walk(e.children);
            }
        }
    };
    const Walker walker{*this, down, clicked};
    for (EntityId id : scene.EntitiesWith<UiDocument>()) {
        const Entity* e = scene.Find(id);
        const UiDocument* document = e ? e->Get<UiDocument>() : nullptr;
        if (UiDocumentData* tree = document ? document->Tree() : nullptr) walker.Walk(tree->elements);
    }
}

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
    if (element->text != text) element->text = std::string(text);
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
