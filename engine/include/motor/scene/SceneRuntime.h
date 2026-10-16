#pragma once

// Loop de jogo de uma cena: chama OnStart/OnUpdate/OnFixedUpdate/OnDestroy dos scripts e os ganchos dos
// sistemas registrados (física, áudio..., ver RuntimeSystem.h, que documenta a ordem). Só existe enquanto
// a cena está "rodando" (Play no editor, ou o jogo exportado). O editor nunca cria um fora do Play.

#include "motor/scene/RuntimeSystem.h"
#include "motor/scene/Script.h"

#include <cstddef>
#include <functional>
#include <memory>
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

    void Start();                 // cria os sistemas; OnStart neles e nos scripts de entidades ativas
    void Update(float dt);        // inicia os que faltam, OnUpdate, aplica as destruições pedidas
    void FixedUpdate(float dt);   // OnFixedUpdate
    void Stop();                  // OnDestroy nos scripts, OnStop nos sistemas; depois disso não roda mais nada

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

    // Sistema do tipo T deste runtime, ou nullptr (não registrado, ou runtime parado).
    template <class T>
    T* GetSystem() {
        static_assert(std::is_base_of_v<RuntimeSystem, T>, "GetSystem: T precisa herdar de motor::RuntimeSystem");
        return static_cast<T*>(FindSystem(typeid(T)));
    }
    // Compara pelo type_info (nome do tipo), então funciona também numa DLL de scripts.
    RuntimeSystem* FindSystem(const std::type_info& type);

private:
    struct SystemSlot {
        RuntimeSystemInfo info;
        std::unique_ptr<RuntimeSystem> system;
    };

    // Ganchos dos sistemas antes (order < 0) ou depois (order >= 0) dos scripts.
    void RunSystems(bool beforeScripts, void (RuntimeSystem::*hook)(SceneRuntime&, float), float dt);
    // Chama fn(script, id) para cada script de entidade ativa na hierarquia. Entidades criadas durante o
    // laço ficam para o próximo frame.
    void ForEachScript(const std::function<void(Script&, EntityId)>& fn);
    void StartPending();
    void FlushDestroyed();
    void Bind(Script& script, EntityId id);

    Scene& m_scene;
    GameTime m_time;
    std::vector<EntityId> m_toDestroy;
    std::vector<SystemSlot> m_systems;  // na ordem de RegisteredRuntimeSystems(); vazio fora do Play
    bool m_running = false;
    bool m_inputEnabled = true;
};

} // namespace motor
