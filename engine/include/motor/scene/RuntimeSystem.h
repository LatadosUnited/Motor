#pragma once

// Sistemas do SceneRuntime: o ponto de extensão para as camadas acima de scene (física, áudio, HUD...) rodarem
// a cada passo do jogo sem que o SceneRuntime as inclua (incluir para cima é proibido, ver test_layers).
//
// Uma camada registra um sistema num .cpp próprio, no estilo de MOTOR_COMPONENT_MODULE:
//
//   class PhysicsSystem : public motor::RuntimeSystem {
//   public:
//       void OnStart(motor::SceneRuntime& runtime) override { /* cria os corpos da cena */ }
//       void OnFixedUpdate(motor::SceneRuntime& runtime, float dt) override { /* avança o mundo */ }
//       void OnStop(motor::SceneRuntime& runtime) override { /* libera os corpos */ }
//   };
//   MOTOR_RUNTIME_SYSTEM(Fisica, PhysicsSystem, motor::kSystemOrderPhysics);
//
// Cada SceneRuntime cria uma instância nova de cada sistema registrado ao iniciar (Start) e a destrói ao parar
// (Stop): o estado do sistema é por runtime (por Play) e começa limpo a cada Play. Os scripts acham o sistema
// com GetSystem<PhysicsSystem>() (Script.h); outros sistemas, com runtime.GetSystem<T>().
//
// Ordem (estável, não depende da ordem de ligação dos .cpp): os sistemas são ordenados pela `order` e, no
// empate, pelo nome. Em cada passo:
//
//   Start:       OnStart de todos os sistemas (order crescente) -> OnStart dos scripts
//   FixedUpdate: OnStart dos scripts pendentes -> OnFixedUpdate dos sistemas com order < 0
//                -> OnFixedUpdate dos scripts -> OnFixedUpdate dos sistemas com order >= 0 -> destruições
//   Update:      OnStart dos scripts pendentes -> OnUpdate dos sistemas com order < 0
//                -> OnUpdate dos scripts -> OnUpdate dos sistemas com order >= 0 -> destruições
//   Stop:        OnDestroy dos scripts -> OnStop de todos os sistemas (order decrescente) -> sistemas destruídos
//
// Assim os scripts já encontram os sistemas prontos no OnStart e ainda podem usá-los no OnDestroy. Um sistema
// que precise rodar antes e depois dos scripts no mesmo passo registra dois sistemas (o segundo acha o
// primeiro com runtime.GetSystem<T>()).
//
// Entidades podem sumir entre dois passos (Destroy dos scripts, exclusão no editor durante o Play): guarde ids
// (EntityId) e confira com scene.Find(id) a cada passo, nunca ponteiros para Entity ou componentes.
//
// Sistemas são da engine. Uma DLL que registra sistemas e é descarregada tira os seus do registro, mas os
// runtimes já iniciados continuam com as instâncias: só descarregue com o Play parado.

#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>

namespace motor {

class SceneRuntime;

// Ordens usadas pelas camadas da engine (constantes aqui, na camada de baixo, para todas verem a mesma
// tabela). Negativa: roda antes dos scripts no FixedUpdate/Update; zero ou positiva: depois.
inline constexpr int kSystemOrderPhysics = -100;  // física avança antes do OnFixedUpdate dos scripts
inline constexpr int kSystemOrderAudio = 100;     // áudio lê as posições depois que os scripts as mudaram
inline constexpr int kSystemOrderUi = 200;        // HUD por último
inline constexpr int kSystemOrderAnimation = 50;  // animação depois dos scripts (que trocam os clipes)

class RuntimeSystem {
public:
    RuntimeSystem() = default;
    virtual ~RuntimeSystem() = default;

    RuntimeSystem(const RuntimeSystem&) = delete;
    RuntimeSystem& operator=(const RuntimeSystem&) = delete;

    virtual void OnStart(SceneRuntime& /*runtime*/) {}
    virtual void OnFixedUpdate(SceneRuntime& /*runtime*/, float /*dt*/) {}
    virtual void OnUpdate(SceneRuntime& /*runtime*/, float /*dt*/) {}
    virtual void OnStop(SceneRuntime& /*runtime*/) {}
};

using RuntimeSystemFactory = std::unique_ptr<RuntimeSystem> (*)();

struct RuntimeSystemInfo {
    std::string name;  // para logs e para desempatar a ordem
    int order = 0;
    const std::type_info* cppType = nullptr;  // achar o sistema de T entre DLLs (ver GetSystem)
    RuntimeSystemFactory create = nullptr;
};

// Registro global dos sistemas. Normalmente usado só pelo MOTOR_RUNTIME_SYSTEM e pelo SceneRuntime.
void RegisterRuntimeSystem(RuntimeSystemInfo info);
void UnregisterRuntimeSystem(const std::type_info& cppType);
// Sistemas registrados, já na ordem em que rodam.
std::vector<RuntimeSystemInfo> RegisteredRuntimeSystems();

template <class T>
struct RuntimeSystemRegistrar {
    static_assert(std::is_base_of_v<RuntimeSystem, T>, "sistemas precisam herdar de motor::RuntimeSystem");
    RuntimeSystemRegistrar(const char* name, int order) {
        RegisterRuntimeSystem({name, order, &typeid(T), []() -> std::unique_ptr<RuntimeSystem> {
                                   return std::make_unique<T>();
                               }});
    }
    ~RuntimeSystemRegistrar() { UnregisterRuntimeSystem(typeid(T)); }
    RuntimeSystemRegistrar(const RuntimeSystemRegistrar&) = delete;
    RuntimeSystemRegistrar& operator=(const RuntimeSystemRegistrar&) = delete;
};

} // namespace motor

#define MOTOR_RUNTIME_SYSTEM(Name, Type, order) \
    static const ::motor::RuntimeSystemRegistrar<Type> g_motorRuntimeSystem_##Name(#Name, order)
