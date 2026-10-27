#include "UndoHistory.h"

#include <motor/core/Profile.h>

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace editor {

namespace {
// Para cada entidade de `scene`, a de `base` (fotos em ordem de id, como a cena) se for igual; senão uma cópia nova.
// `reuse` (opcional): cópias já feitas a partir de uma entidade antiga, para fotos diferentes que tinham a mesma
// entidade continuarem dividindo a cópia (ForEachScene).
std::vector<std::shared_ptr<const motor::Entity>> ShareEntities(
    const motor::Scene& scene, const std::vector<std::shared_ptr<const motor::Entity>>& base,
    std::unordered_map<const motor::Entity*, std::shared_ptr<const motor::Entity>>* reuse = nullptr) {
    std::vector<std::shared_ptr<const motor::Entity>> out;
    out.reserve(scene.Entities().size());
    size_t j = 0;
    for (const auto& e : scene.Entities()) {
        while (j < base.size() && base[j]->id < e->id) ++j;
        const motor::Entity* old = j < base.size() && base[j]->id == e->id ? base[j].get() : nullptr;
        if (old && motor::SameEntity(*e, *old)) {
            out.push_back(base[j]);
            continue;
        }
        if (old && reuse) {
            if (auto it = reuse->find(old); it != reuse->end() && motor::SameEntity(*e, *it->second)) {
                out.push_back(it->second);
                continue;
            }
        }
        out.push_back(std::make_shared<const motor::Entity>(*e));
        if (old && reuse) (*reuse)[old] = out.back();
    }
    return out;
}
} // namespace

UndoHistory::State UndoHistory::Snapshot(const motor::Scene& scene, const State& base) {
    State state;
    state.entities = ShareEntities(scene, base.entities);
    state.environment = scene.environment;
    state.nextId = scene.NextEntityId();
    return state;
}

bool UndoHistory::Differs(const motor::Scene& scene, const State& state) {
    MOTOR_PROFILE_SCOPE("Desfazer: comparar cena");  // custo que cresce com a cena (alpha 3)
    const auto& entities = scene.Entities();
    if (entities.size() != state.entities.size() || scene.environment != state.environment) return true;
    for (size_t i = 0; i < entities.size(); ++i) {
        if (!motor::SameEntity(*entities[i], *state.entities[i])) return true;
    }
    return false;
}

motor::Scene UndoHistory::Materialize(const State& state) {
    std::vector<const motor::Entity*> entities;
    entities.reserve(state.entities.size());
    for (const auto& e : state.entities) entities.push_back(e.get());
    return motor::Scene::FromEntities(entities, state.nextId, state.environment);
}

void UndoHistory::Reset(const motor::Scene& scene, motor::EntityId selected) {
    m_current = Snapshot(scene, {});
    m_current.version = m_nextVersion++;
    m_current.selected = selected;
    m_undo.clear();
    m_redo.clear();
    m_activity = false;
    m_lastCompare = std::chrono::steady_clock::now();
}

void UndoHistory::Track(const motor::Scene& scene, motor::EntityId selected, bool idle,
                        const std::vector<motor::EntityId>* selection) {
    if (!idle) return;
    // Nada que pudesse mudar a cena desde a última comparação: espera a próxima atividade ou o intervalo.
    const auto now = std::chrono::steady_clock::now();
    if (m_lazy && !m_activity && now - m_lastCompare < std::chrono::duration<double>(kIdleCompareSeconds)) return;
    TrackNow(scene, selected, selection);
}

void UndoHistory::Commit(const motor::Scene& scene, motor::EntityId selected,
                         const std::vector<motor::EntityId>* selection) {
    TrackNow(scene, selected, selection);
}

void UndoHistory::TrackNow(const motor::Scene& scene, motor::EntityId selected,
                           const std::vector<motor::EntityId>* selection) {
    m_activity = false;
    m_lastCompare = std::chrono::steady_clock::now();
    if (!Differs(scene, m_current)) {
        // Só a seleção mudou: não é um passo, mas a foto guarda a seleção atual para o próximo passo.
        m_current.selected = selected;
        m_current.selection = selection ? *selection : std::vector<motor::EntityId>{};
        return;
    }
    State next = Snapshot(scene, m_current);  // só as entidades que mudaram são copiadas
    next.version = m_nextVersion++;
    next.selected = selected;
    next.selection = selection ? *selection : std::vector<motor::EntityId>{};
    m_undo.push_back(std::move(m_current));
    if (m_undo.size() > kMaxSteps) m_undo.pop_front();
    m_redo.clear();  // uma alteração nova descarta o que podia ser refeito
    m_current = std::move(next);
}

bool UndoHistory::HasPendingChange(const motor::Scene& scene) const {
    return Differs(scene, m_current);
}

void UndoHistory::PushAction(const motor::Scene& scene, motor::EntityId selected,
                             const std::vector<motor::EntityId>* selection, std::function<void()> undo,
                             std::function<void()> redo, bool unsaved) {
    TrackNow(scene, selected, selection);
    State step;
    const uint64_t before = m_current.version;
    if (unsaved) m_current.version = m_nextVersion++;
    step.action = std::make_shared<State::Action>(
        State::Action{std::move(undo), std::move(redo), before, m_current.version});
    m_undo.push_back(std::move(step));
    if (m_undo.size() > kMaxSteps) m_undo.pop_front();
    m_redo.clear();
}

bool UndoHistory::Undo(motor::Scene& scene, motor::EntityId& selected, std::vector<motor::EntityId>* selection) {
    TrackNow(scene, selected, selection);  // registra antes uma mudança que ainda não virou passo
    if (m_undo.empty()) return false;

    State previous = std::move(m_undo.back());
    m_undo.pop_back();
    if (previous.action) {  // a cena não muda: só o asset
        previous.action->undo();
        m_current.version = previous.action->versionBefore;
        m_redo.push_back(std::move(previous));
        return true;
    }
    m_redo.push_back(std::move(m_current));  // igual à cena, com a seleção atual (TrackNow)
    Restore(std::move(previous), scene, selected, selection);
    return true;
}

bool UndoHistory::Redo(motor::Scene& scene, motor::EntityId& selected, std::vector<motor::EntityId>* selection) {
    TrackNow(scene, selected, selection);  // se havia mudança pendente, ela limpa o refazer (como deve)
    if (m_redo.empty()) return false;

    State next = std::move(m_redo.back());
    m_redo.pop_back();
    if (next.action) {
        next.action->redo();
        m_current.version = next.action->versionAfter;
        m_undo.push_back(std::move(next));
        return true;
    }
    m_undo.push_back(std::move(m_current));
    Restore(std::move(next), scene, selected, selection);
    return true;
}

void UndoHistory::Restore(State&& state, motor::Scene& scene, motor::EntityId& selected,
                          std::vector<motor::EntityId>* selection) {
    scene = Materialize(state);
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
    std::unordered_map<const motor::Entity*, std::shared_ptr<const motor::Entity>> reuse;
    auto apply = [&](State& state) {
        if (state.action) return;
        motor::Scene scene = Materialize(state);
        fn(scene);
        state.entities = ShareEntities(scene, state.entities, &reuse);
        state.environment = scene.environment;
        state.nextId = scene.NextEntityId();
    };
    apply(m_current);
    for (State& s : m_undo) apply(s);
    for (State& s : m_redo) apply(s);
}

} // namespace editor
