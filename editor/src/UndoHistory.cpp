#include "UndoHistory.h"

#include <algorithm>
#include <utility>

namespace editor {

UndoHistory::State UndoHistory::Snapshot(const motor::Scene& scene, motor::EntityId selected,
                                         const std::vector<motor::EntityId>* selection) {
    return {scene, selected, selection ? *selection : std::vector<motor::EntityId>{}};
}

void UndoHistory::Reset(const motor::Scene& scene, motor::EntityId selected) {
    m_current = {scene, selected, {}};
    m_undo.clear();
    m_redo.clear();
}

void UndoHistory::Track(const motor::Scene& scene, motor::EntityId selected, bool idle,
                        const std::vector<motor::EntityId>* selection) {
    if (!idle) return;
    if (scene == m_current.scene) {
        // Só a seleção mudou: não é um passo, mas a foto guarda a seleção atual para o próximo passo.
        m_current.selected = selected;
        m_current.selection = selection ? *selection : std::vector<motor::EntityId>{};
        return;
    }
    m_undo.push_back(std::move(m_current));
    if (m_undo.size() > kMaxSteps) m_undo.pop_front();
    m_redo.clear();  // uma alteração nova descarta o que podia ser refeito
    m_current = Snapshot(scene, selected, selection);
}

bool UndoHistory::Undo(motor::Scene& scene, motor::EntityId& selected, std::vector<motor::EntityId>* selection) {
    Track(scene, selected, true, selection);  // registra antes uma mudança que ainda não virou passo
    if (m_undo.empty()) return false;

    State previous = std::move(m_undo.back());
    m_undo.pop_back();
    m_redo.push_back(Snapshot(scene, selected, selection));
    Restore(std::move(previous), scene, selected, selection);
    return true;
}

bool UndoHistory::Redo(motor::Scene& scene, motor::EntityId& selected, std::vector<motor::EntityId>* selection) {
    Track(scene, selected, true, selection);  // se havia mudança pendente, ela limpa o refazer (como deve)
    if (m_redo.empty()) return false;

    State next = std::move(m_redo.back());
    m_redo.pop_back();
    m_undo.push_back(Snapshot(scene, selected, selection));
    Restore(std::move(next), scene, selected, selection);
    return true;
}

void UndoHistory::Restore(State&& state, motor::Scene& scene, motor::EntityId& selected,
                          std::vector<motor::EntityId>* selection) {
    scene = state.scene;
    // Seleção: a da foto, se ainda existir; senão mantém a atual, se ela ainda existir.
    if (scene.Find(state.selected)) {
        selected = state.selected;
        if (selection) *selection = state.selection;
    } else if (!scene.Find(selected)) {
        selected = motor::kNullEntity;
        if (selection) selection->clear();
    }
    if (selection) std::erase_if(*selection, [&](motor::EntityId id) { return !scene.Find(id); });
    m_current = std::move(state);
    m_current.selected = selected;
    m_current.selection = selection ? *selection : std::vector<motor::EntityId>{};
}

void UndoHistory::ForEachScene(const std::function<void(motor::Scene&)>& fn) {
    fn(m_current.scene);
    for (State& s : m_undo) fn(s.scene);
    for (State& s : m_redo) fn(s.scene);
}

} // namespace editor
