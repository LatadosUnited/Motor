#pragma once

#include <motor/scene/Scene.h>

#include <cstddef>
#include <deque>
#include <functional>
#include <vector>

namespace editor {

// Desfazer/refazer por "fotos" da cena. Em vez de cada painel registrar comandos, o editor chama Track()
// uma vez por frame: se a cena mudou desde a última foto, a foto anterior vira um passo de desfazer.
//
// Enquanto o usuário está no meio de uma interação (arrastando o gizmo, um slider, digitando um nome), o
// editor passa idle = false e nada é registrado; assim um arraste inteiro vira um passo só.
class UndoHistory {
public:
    static constexpr size_t kMaxSteps = 100;

    // Começa do zero (nova cena, cena aberta do disco).
    void Reset(const motor::Scene& scene, motor::EntityId selected);

    void Track(const motor::Scene& scene, motor::EntityId selected, bool idle);

    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }
    size_t UndoCount() const { return m_undo.size(); }
    size_t RedoCount() const { return m_redo.size(); }

    // Trocam o conteúdo de `scene` e ajustam a seleção. Retornam false se não havia o que fazer.
    bool Undo(motor::Scene& scene, motor::EntityId& selected);
    bool Redo(motor::Scene& scene, motor::EntityId& selected);

    // Todas as fotos guardadas (a recarga dos scripts precisa trocar os componentes delas também).
    void ForEachScene(const std::function<void(motor::Scene&)>& fn);

private:
    struct State {
        motor::Scene scene;
        motor::EntityId selected = motor::kNullEntity;  // seleção de quando a foto foi tirada
    };

    void Restore(State&& state, motor::Scene& scene, motor::EntityId& selected);

    State m_current;                // última foto: igual à cena quando não há mudança pendente
    std::deque<State> m_undo;       // mais antiga na frente
    std::vector<State> m_redo;
};

} // namespace editor
