#include "motor/ui/UiSystem.h"

#include "motor/scene/Scene.h"
#include "motor/scene/SceneRuntime.h"

namespace motor {

MOTOR_RUNTIME_SYSTEM(Ui, UiSystem, kSystemOrderUi);

void UiSystem::OnStart(SceneRuntime& runtime) {
    m_runtime = &runtime;
    m_layout.clear();
    m_hovered = m_pressed = 0;
    m_wasDown = false;
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
    if (m_pointer.inside) {
        const EntityId top = UiHitTest(scene, m_layout, m_pointer.position);
        const Entity* e = scene.Find(top);
        const UiButton* button = e ? e->Get<UiButton>() : nullptr;
        if (button && button->interactable) hovered = top;
    }
    const bool down = m_pointer.down;
    const bool pressedNow = down && !m_wasDown;
    const bool released = !down && m_wasDown;
    EntityId clicked = 0;
    if (pressedNow) m_pressed = hovered;  // clique que começa fora de um botão não vale para nenhum
    if (released) {
        if (m_pressed && m_pressed == hovered) clicked = m_pressed;
        m_pressed = 0;
    }
    m_wasDown = down;
    m_hovered = hovered;

    // O estado vai nos componentes (o editor mostra a cor certa; os scripts leem com WasClicked).
    for (EntityId id : scene.EntitiesWith<UiButton>()) {
        Entity* e = scene.Find(id);
        UiButton* button = e ? e->Get<UiButton>() : nullptr;
        if (!button) continue;
        button->hovered = id == hovered;
        button->pressed = id == m_pressed && down && id == hovered;
        button->clicked = id == clicked;
    }
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
    const Entity* e = FindEntity(entity);
    return e && SetText(e->id, text);
}

std::string UiSystem::GetText(std::string_view entity) const {
    const Entity* e = FindEntity(entity);
    const UiText* ui = e ? e->Get<UiText>() : nullptr;
    return ui ? ui->text : std::string();
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
    const Entity* e = FindEntity(entity);
    return e && SetVisible(e->id, visible);
}

bool UiSystem::IsVisible(std::string_view entity) const {
    const Entity* e = FindEntity(entity);
    if (!e || !m_runtime) return false;
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
    const Entity* e = FindEntity(entity);
    return e && WasClicked(e->id);
}

bool UiSystem::ScreenRect(EntityId entity, UiBox& out) const {
    for (const UiNode& node : m_layout) {
        if (node.entity == entity) {
            out = node.box;
            return true;
        }
    }
    return false;
}

} // namespace motor
