#pragma once

#include <motor/scene/Scene.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
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
//
// Cena grande (Fase 27): a foto guarda as entidades como cópias imutáveis compartilhadas entre as fotos, e cada
// passo novo copia só as entidades que mudaram. Com SetLazyCompare(true) (o editor), a comparação com a cena só roda
// quando algo pode ter mudado (NoteActivity: entrada do usuário, pedido do MCP) ou a cada kIdleCompareSeconds, para
// pegar o resto (prefab alterado no disco, navmesh gerado em segundo plano...).
class UndoHistory {
public:
    static constexpr size_t kMaxSteps = 100;
    static constexpr double kIdleCompareSeconds = 0.25;

    // Começa do zero (nova cena, cena aberta do disco).
    void Reset(const motor::Scene& scene, motor::EntityId selected);

    // Track só compara depois de NoteActivity ou de kIdleCompareSeconds (desligado: compara sempre).
    void SetLazyCompare(bool lazy) { m_lazy = lazy; }
    // Algo pode ter mudado a cena neste quadro: o próximo Track compara mesmo antes de kIdleCompareSeconds.
    void NoteActivity() { m_activity = true; }

    // `selection` (opcional): todos os selecionados (seleção múltipla), guardados e devolvidos junto com a foto.
    void Track(const motor::Scene& scene, motor::EntityId selected, bool idle,
               const std::vector<motor::EntityId>* selection = nullptr);
    // O mesmo, comparando agora (antes de salvar, desfazer...).
    void Commit(const motor::Scene& scene, motor::EntityId selected, const std::vector<motor::EntityId>* selection);

    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }
    size_t UndoCount() const { return m_undo.size(); }
    size_t RedoCount() const { return m_redo.size(); }

    // Identifica o estado da última foto: muda a cada passo novo e volta ao valor de antes ao desfazer. Igual ao
    // de quando a cena foi salva (e sem HasPendingChange) = nada a salvar, sem gravar a cena em JSON.
    uint64_t Version() const { return m_current.version; }
    // A cena tem mudança que ainda não virou passo (compara agora).
    bool HasPendingChange(const motor::Scene& scene) const;

    // Trocam o conteúdo de `scene` e ajustam a seleção. Retornam false se não havia o que fazer.
    bool Undo(motor::Scene& scene, motor::EntityId& selected, std::vector<motor::EntityId>* selection = nullptr);
    bool Redo(motor::Scene& scene, motor::EntityId& selected, std::vector<motor::EntityId>* selection = nullptr);

    // Passo que não é da cena (material ou prefab editado no Inspector): entra na mesma fila do desfazer, com as
    // funções que voltam e refazem a mudança. Registra antes uma mudança da cena que ainda não virou passo.
    void PushAction(const motor::Scene& scene, motor::EntityId selected, const std::vector<motor::EntityId>* selection,
                    std::function<void()> undo, std::function<void()> redo);

    // Todas as fotos guardadas, cada uma montada como cena (a recarga dos scripts e os prefabs precisam trocar os
    // componentes delas também). Caro com cena grande e histórico longo: cada foto vira uma cópia da cena.
    void ForEachScene(const std::function<void(motor::Scene&)>& fn);

private:
    using EntityPtr = std::shared_ptr<const motor::Entity>;
    struct State {
        std::vector<EntityPtr> entities;  // em ordem de id, como Scene::Entities()
        motor::Environment environment;
        motor::EntityId nextId = 1;
        uint64_t version = 0;
        motor::EntityId selected = motor::kNullEntity;  // seleção de quando a foto foi tirada
        std::vector<motor::EntityId> selection;
        // Passo de fora da cena (PushAction): a foto acima não é usada.
        struct Action {
            std::function<void()> undo, redo;
        };
        std::shared_ptr<Action> action;
    };

    // Foto de `scene` reaproveitando as entidades iguais de `base`.
    static State Snapshot(const motor::Scene& scene, const State& base);
    static bool Differs(const motor::Scene& scene, const State& state);
    static motor::Scene Materialize(const State& state);
    void TrackNow(const motor::Scene& scene, motor::EntityId selected, const std::vector<motor::EntityId>* selection);
    void Restore(State&& state, motor::Scene& scene, motor::EntityId& selected, std::vector<motor::EntityId>* selection);

    State m_current;                // última foto: igual à cena quando não há mudança pendente
    std::deque<State> m_undo;       // mais antiga na frente
    std::vector<State> m_redo;
    uint64_t m_nextVersion = 1;
    bool m_lazy = false;
    bool m_activity = true;
    std::chrono::steady_clock::time_point m_lastCompare{};
};

} // namespace editor
