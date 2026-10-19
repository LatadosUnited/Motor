#pragma once

#include <motor/scene/Scene.h>

#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
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

    // `selection` (opcional): todos os selecionados (seleção múltipla), guardados e devolvidos junto com a foto.
    void Track(const motor::Scene& scene, motor::EntityId selected, bool idle,
               const std::vector<motor::EntityId>* selection = nullptr);

    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }
    size_t UndoCount() const { return m_undo.size(); }
    size_t RedoCount() const { return m_redo.size(); }

    // Trocam o conteúdo de `scene` e ajustam a seleção. Retornam false se não havia o que fazer.
    bool Undo(motor::Scene& scene, motor::EntityId& selected, std::vector<motor::EntityId>* selection = nullptr);
    bool Redo(motor::Scene& scene, motor::EntityId& selected, std::vector<motor::EntityId>* selection = nullptr);

    // Passo que não é da cena (material ou prefab editado no Inspector): entra na mesma fila do desfazer, com as
    // funções que voltam e refazem a mudança. Registra antes uma mudança da cena que ainda não virou passo.
    void PushAction(const motor::Scene& scene, motor::EntityId selected, const std::vector<motor::EntityId>* selection,
                    std::function<void()> undo, std::function<void()> redo);

    // Todas as fotos guardadas (a recarga dos scripts precisa trocar os componentes delas também).
    void ForEachScene(const std::function<void(motor::Scene&)>& fn);

private:
    struct State {
        motor::Scene scene;
        motor::EntityId selected = motor::kNullEntity;  // seleção de quando a foto foi tirada
        std::vector<motor::EntityId> selection;
        // Passo de fora da cena (PushAction): a cena acima não é usada.
        struct Action {
            std::function<void()> undo, redo;
        };
        std::shared_ptr<Action> action;
    };

    static State Snapshot(const motor::Scene& scene, motor::EntityId selected,
                          const std::vector<motor::EntityId>* selection);
    void Restore(State&& state, motor::Scene& scene, motor::EntityId& selected, std::vector<motor::EntityId>* selection);

    State m_current;                // última foto: igual à cena quando não há mudança pendente
    std::deque<State> m_undo;       // mais antiga na frente
    std::vector<State> m_redo;
};

} // namespace editor
