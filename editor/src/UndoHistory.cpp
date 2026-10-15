#include "UndoHistory.h"

#include <utility>

namespace editor {

void UndoHistory::Reset(const motor::Scene& scene, motor::EntityId selected) {
    m_current = {scene, selected};
    m_undo.clear();
    m_redo.clear();
}

void UndoHistory::Track(const motor::Scene& scene, motor::EntityId selected, bool idle) {
    if (!idle) return;
    if (scene == m_current.scene) {
        // Só a seleção mudou: não é um passo, mas a foto guarda a seleção atual para o próximo passo.
        m_current.selected = selected;
        return;
    }
    m_undo.push_back(std::move(m_current));
    if (m_undo.size() > kMaxSteps) m_undo.pop_front();
    m_redo.clear();  // uma alteração nova descarta o que podia ser refeito
    m_current = {scene, selected};
}

bool UndoHistory::Undo(motor::Scene& scene, motor::EntityId& selected) {
    Track(scene, selected, true);  // registra antes uma mudança que ainda não virou passo
    if (m_undo.empty()) return false;

    State previous = std::move(m_undo.back());
    m_undo.pop_back();
    m_redo.push_back({scene, selected});
    Restore(std::move(previous), scene, selected);
    return true;
}

bool UndoHistory::Redo(motor::Scene& scene, motor::EntityId& selected) {
    Track(scene, selected, true);  // se havia mudança pendente, ela limpa o refazer (como deve)
    if (m_redo.empty()) return false;

    State next = std::move(m_redo.back());
    m_redo.pop_back();
    m_undo.push_back({scene, selected});
    Restore(std::move(next), scene, selected);
    return true;
}

void UndoHistory::Restore(State&& state, motor::Scene& scene, motor::EntityId& selected) {
    scene = state.scene;
    // Seleção: a da foto, se ainda existir; senão mantém a atual, se ela ainda existir.
    if (scene.Find(state.selected)) {
        selected = state.selected;
    } else if (!scene.Find(selected)) {
        selected = motor::kNullEntity;
    }
    m_current = std::move(state);
    m_current.selected = selected;
}

} // namespace editor
