#pragma once

// Loop de jogo de uma cena: chama OnStart/OnUpdate/OnFixedUpdate/OnDestroy dos scripts e os ganchos dos
// sistemas registrados (física, áudio..., ver RuntimeSystem.h, que documenta a ordem). Só existe enquanto
// a cena está "rodando" (Play no editor, ou o jogo exportado). O editor nunca cria um fora do Play.

#include "motor/scene/RuntimeSystem.h"
#include "motor/scene/Script.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace motor {

class AssetLibrary;
class PrefabLibrary;
struct ComponentType;
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

    // Biblioteca de assets do projeto (opcional): usada por Instantiate para carregar os prefabs com as malhas,
    // texturas e materiais do projeto. Sem ela, Instantiate usa uma biblioteca local se `prefab.file` existir.
    void SetAssets(AssetLibrary* assets) { m_assets = assets; }

    // --- API usada pelos scripts ---
    Entity* FindEntity(std::string_view name);
    Entity& CreateEntity(std::string name, EntityId parent);
    EntityId Instantiate(const PrefabRef& prefab);
    EntityId Instantiate(const PrefabRef& prefab, const glm::vec3& position);
    void Destroy(EntityId id);  // adiada para o fim do passo atual

    // Temporizadores (Script::After/Every): rodam no Update, antes do OnUpdate dos scripts, no tempo do jogo.
    TimerId AddTimer(Script& owner, EntityId entity, float seconds, bool repeat, std::function<void()> fn);
    void CancelTimer(TimerId id);
    void CancelTimers(const Script& owner);
    size_t TimerCount() const { return m_timers.size(); }

    // Mensagens (Script::Send/Broadcast): OnMessage nos scripts iniciados da entidade, ou de todas.
    void Send(EntityId target, const Message& message);
    void Broadcast(const Message& message);

    // Números aleatórios deste Play (semente nova a cada Start).
    float Random(float min, float max);
    int RandomInt(int min, int max);
    void SetRandomSeed(uint32_t seed) { m_random.seed(seed); }

    // Recarga da DLL de scripts durante o Play: os scripts vão ser destruídos e recriados (os novos começam
    // não iniciados e recebem OnStart no próximo passo). Antes disso, solta o que aponta para os objetos e o
    // código antigos: temporizadores e scripts marcados com erro.
    void ForgetScripts();
    // Cenas dos prefabs carregados por Instantiate (também têm scripts, para a recarga).
    void ForEachPrefabScene(const std::function<void(Scene&)>& fn);

    // Troca de cena pedida por um script (Script::LoadScene). O runtime só guarda o pedido: quem roda o jogo
    // (o Play do editor, o jogo exportado) confere depois de cada passo, para este runtime e começa a cena
    // nova com um runtime novo. Vários pedidos no mesmo passo: vale o último.
    void RequestSceneLoad(std::string scene) { m_sceneRequest = std::move(scene); }
    bool HasSceneRequest() const { return !m_sceneRequest.empty(); }
    std::string TakeSceneRequest() { return std::exchange(m_sceneRequest, {}); }

    // Sistema do tipo T deste runtime, ou nullptr (não registrado, ou runtime parado).
    template <class T>
    T* GetSystem() {
        static_assert(std::is_base_of_v<RuntimeSystem, T>, "GetSystem: T precisa herdar de motor::RuntimeSystem");
        return static_cast<T*>(FindSystem(typeid(T)));
    }
    // Compara pelo type_info (nome do tipo), então funciona também numa DLL de scripts.
    RuntimeSystem* FindSystem(const std::type_info& type);

    // Chama `fn` em cada script já iniciado e ativo da entidade `id`, com proteção contra exceções.
    // Usado por sistemas (ex.: eventos de física) para despachar callbacks nos scripts.
    void CallEntityScripts(EntityId id, std::string_view callback, const std::function<void(Script&)>& fn);

    // Exceções capturadas em scripts ou sistemas: o script/sistema que lançou é desativado neste Play
    // e o erro vai para o log. O editor usa TakeError() para pausar o Play quando um erro novo acontece.
    bool HasError() const { return !m_lastError.empty(); }
    const std::string& LastError() const { return m_lastError; }
    bool TakeError() { return std::exchange(m_hasNewError, false); }
    int ErrorCount() const { return m_errorCount; }

private:
    struct SystemSlot {
        RuntimeSystemInfo info;
        std::unique_ptr<RuntimeSystem> system;
        bool faulted = false;
    };

    // Ganchos dos sistemas antes (order < 0) ou depois (order >= 0) dos scripts.
    void RunSystems(bool beforeScripts, void (RuntimeSystem::*hook)(SceneRuntime&, float), float dt);
    // Chama fn(script, id, scriptName) para cada script não falho de entidade ativa na hierarquia.
    // Entidades criadas durante o laço ficam para o próximo frame.
    void ForEachScript(const std::function<void(Script&, EntityId, std::string_view)>& fn);
    void InvokeScript(Script& script, EntityId id, std::string_view scriptName, std::string_view callback,
                      const std::function<void()>& fn);
    void InvokeSystem(SystemSlot& slot, std::string_view callback, const std::function<void()>& fn);
    void ReportScriptError(EntityId id, std::string_view scriptName, std::string_view callback,
                           std::string_view what);
    void ReportSystemError(std::string_view systemName, std::string_view callback, std::string_view what);
    void StartPending();
    void RunTimers();
    // Tipo do componente de script `script` na entidade `id`, ou nullptr se ela não o tem mais.
    const ComponentType* ScriptTypeOf(EntityId id, const Script* script);
    void FlushDestroyed();
    void Bind(Script& script, EntityId id);

    Scene& m_scene;
    AssetLibrary* m_assets = nullptr;
    std::unique_ptr<PrefabLibrary> m_prefabs;
    std::unique_ptr<AssetLibrary> m_fallbackAssets;
    GameTime m_time;
    std::vector<EntityId> m_toDestroy;
    std::vector<SystemSlot> m_systems;  // na ordem de RegisteredRuntimeSystems(); vazio fora do Play
    std::unordered_set<const Script*> m_faultedScripts;
    std::string m_lastError;
    bool m_hasNewError = false;
    int m_errorCount = 0;
    bool m_running = false;
    bool m_inputEnabled = true;
    std::string m_sceneRequest;

    struct Timer {
        TimerId id = 0;
        Script* owner = nullptr;
        EntityId entity = 0;
        double due = 0.0;  // m_time.elapsed em que dispara
        float interval = 0.0f;
        bool repeat = false;
        std::function<void()> fn;
    };
    std::vector<Timer> m_timers;
    TimerId m_nextTimer = 1;
    std::mt19937 m_random;
};

// Arquivo de cena de um pedido de LoadScene: caminho relativo ao projeto ("Assets/Cenas/Fase1.mscene", a
// extensão é opcional) ou só o nome ("Fase1"), procurado em todas as cenas do projeto. Vazio se não achar.
std::filesystem::path FindSceneFile(const AssetLibrary& assets, std::string_view scene);

} // namespace motor
