#pragma once

// Loop de jogo de uma cena: chama OnStart/OnUpdate/OnFixedUpdate/OnDestroy dos scripts. Só existe enquanto
// a cena está "rodando" (Play no editor, ou o jogo exportado). O editor nunca cria um fora do Play.

#include "motor/scene/Script.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

class Scene;
struct Entity;

class SceneRuntime {
public:
    explicit SceneRuntime(Scene& scene);
    ~SceneRuntime();

    SceneRuntime(const SceneRuntime&) = delete;
    SceneRuntime& operator=(const SceneRuntime&) = delete;

    void Start();                 // OnStart em todos os scripts de entidades ativas
    void Update(float dt);        // inicia os que faltam, OnUpdate, aplica as destruições pedidas
    void FixedUpdate(float dt);   // OnFixedUpdate
    void Stop();                  // OnDestroy em todos que iniciaram; depois disso não roda mais nada

    bool IsRunning() const { return m_running; }
    Scene& GetScene() { return m_scene; }
    const GameTime& Time() const { return m_time; }

    // Desligado quando o jogo não tem o foco: os scripts leem teclado/mouse como soltos.
    void SetInputEnabled(bool enabled) { m_inputEnabled = enabled; }
    bool InputEnabled() const { return m_inputEnabled; }

    // --- API usada pelos scripts ---
    Entity* FindEntity(std::string_view name);
    Entity& CreateEntity(std::string name, EntityId parent);
    void Destroy(EntityId id);  // adiada para o fim do passo atual

private:
    // Chama fn(script, id) para cada script de entidade ativa na hierarquia. Entidades criadas durante o
    // laço ficam para o próximo frame.
    void ForEachScript(const std::function<void(Script&, EntityId)>& fn);
    void StartPending();
    void FlushDestroyed();
    void Bind(Script& script, EntityId id);

    Scene& m_scene;
    GameTime m_time;
    std::vector<EntityId> m_toDestroy;
    bool m_running = false;
    bool m_inputEnabled = true;
};

} // namespace motor
