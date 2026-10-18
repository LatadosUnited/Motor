#pragma once

// Scripts em C++: um script é um componente que herda de motor::Script. Os campos registrados aparecem no
// Inspector, são salvos na cena e entram no desfazer como em qualquer componente; os métodos On* só rodam
// no modo Play (SceneRuntime).
//
//   struct Girar : motor::Script {
//       float velocidade = 90.0f;
//       void OnUpdate(float dt) override { GetTransform().Rotate(...); }
//   };
//   registry.Register<Girar>("Girar", "Girar").Field("velocidade", "Velocidade", &Girar::velocidade);

#include "motor/core/Input.h"
#include "motor/scene/RuntimeSystem.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>

namespace motor {

struct Animator;
struct Entity;
struct PrefabRef;
struct Transform;
class Scene;
class SceneRuntime;
using EntityId = uint32_t;

namespace detail {
// Argumento de Script::Log empacotado sem puxar <format> em cada script.
struct LogArg {
    enum class Kind : uint8_t { Int, UInt, Double, Bool, String } kind;
    union {
        long long i;
        unsigned long long u;
        double d;
        bool b;
    };
    std::string_view s{};

    LogArg(bool v) : kind(Kind::Bool), b(v) {}
    LogArg(char v) : kind(Kind::Int), i(v) {}
    LogArg(signed char v) : kind(Kind::Int), i(v) {}
    LogArg(short v) : kind(Kind::Int), i(v) {}
    LogArg(int v) : kind(Kind::Int), i(v) {}
    LogArg(long v) : kind(Kind::Int), i(v) {}
    LogArg(long long v) : kind(Kind::Int), i(v) {}
    LogArg(unsigned char v) : kind(Kind::UInt), u(v) {}
    LogArg(unsigned short v) : kind(Kind::UInt), u(v) {}
    LogArg(unsigned int v) : kind(Kind::UInt), u(v) {}
    LogArg(unsigned long v) : kind(Kind::UInt), u(v) {}
    LogArg(unsigned long long v) : kind(Kind::UInt), u(v) {}
    LogArg(float v) : kind(Kind::Double), d(v) {}
    LogArg(double v) : kind(Kind::Double), d(v) {}
    LogArg(const char* v) : kind(Kind::String), i(0), s(v ? v : "") {}
    LogArg(std::string_view v) : kind(Kind::String), i(0), s(v) {}
    LogArg(const std::string& v) : kind(Kind::String), i(0), s(v) {}
};
} // namespace detail

// Tempo do jogo (não conta o tempo em pausa).
struct GameTime {
    float delta = 0.0f;       // segundos desde o último OnUpdate
    float fixedDelta = 0.0f;  // passo do OnFixedUpdate
    double elapsed = 0.0;     // segundos desde o Play
    uint64_t frame = 0;       // quantos OnUpdate já rodaram
};

// Contato que começou entre dois corpos da física (OnCollisionEnter).
struct Collision {
    EntityId other = 0;          // a outra entidade
    glm::vec3 point{0.0f};       // ponto de contato, no mundo
    glm::vec3 normal{0.0f};      // aponta do outro corpo para este (para onde este é empurrado)
    float relativeSpeed = 0.0f;  // velocidade de aproximação no contato (m/s); 0 se só encostaram
};

class Script {
public:
    Script() = default;
    virtual ~Script() = default;

    // Copiar um script (cópia da cena para o Play, duplicar entidade) copia só os campos da classe filha:
    // a cópia começa sem dono e ainda não iniciada.
    Script(const Script&) {}
    Script& operator=(const Script&) { return *this; }

    virtual void OnStart() {}               // antes do primeiro OnUpdate (ou quando a entidade fica ativa)
    virtual void OnUpdate(float /*dt*/) {}  // todo frame
    virtual void OnFixedUpdate(float /*dt*/) {}  // passo fixo (física, lógica determinística)
    virtual void OnDestroy() {}             // ao parar o Play ou ao destruir a entidade

    // Física (PhysicsSystem): chamados depois do passo da física, antes do OnFixedUpdate. Colisão precisa de
    // Collider nas duas entidades e de um Rigidbody (ou controlador de personagem) em pelo menos uma; trigger
    // é um Collider com "Trigger" marcado. Os dois lados recebem o evento.
    virtual void OnCollisionEnter(const Collision& /*collision*/) {}
    virtual void OnCollisionExit(EntityId /*other*/) {}
    virtual void OnTriggerEnter(EntityId /*other*/) {}
    virtual void OnTriggerExit(EntityId /*other*/) {}

    bool HasStarted() const { return m_started; }

protected:
    // --- Dono ---
    Entity& GetEntity();
    EntityId GetId() const { return m_entity; }
    Transform& GetTransform();
    Scene& GetScene();

    // --- Cena ---
    Entity* FindEntity(std::string_view name);  // primeira com esse nome, ou nullptr
    Entity& CreateEntity(std::string name, EntityId parent = 0);
    // Instancia um prefab (.mprefab referenciado por um campo PrefabRef) na cena; devolve a entidade raiz criada
    // (ou nullptr se o prefab estiver vazio/ausente). Os scripts da instância iniciam (OnStart) no próximo passo.
    Entity* Instantiate(const PrefabRef& prefab);
    Entity* Instantiate(const PrefabRef& prefab, const glm::vec3& position);
    void Destroy(EntityId id);  // no fim do frame (com os descendentes); OnDestroy é chamado antes
    void DestroySelf();
    // Troca de cena no fim deste passo: a cena atual para (OnDestroy) e a nova começa do zero. `scene`: nome
    // ("Fase1") ou caminho relativo ao projeto ("Assets/Cenas/Fase1.mscene"). No editor, Parar volta à cena
    // de antes do Play.
    void LoadScene(std::string_view scene);

    // --- Tempo e entrada ---
    const GameTime& Time() const;
    // Iguais aos de motor::Input, mas falsos/zero quando o jogo não tem o foco (ex.: digitando no editor).
    bool IsKeyDown(Key key) const;
    bool WasKeyPressed(Key key) const;
    bool IsMouseDown(MouseButton button) const;
    glm::vec2 MouseDelta() const;

    // --- Sistemas do jogo (física, áudio..., ver RuntimeSystem.h) ---
    // Sistema do tipo T deste Play, ou nullptr (não registrado). Já existe no OnStart e ainda no OnDestroy.
    //   if (auto* fisica = GetSystem<motor::PhysicsSystem>()) fisica->...
    template <class T>
    T* GetSystem() {
        static_assert(std::is_base_of_v<RuntimeSystem, T>, "GetSystem: T precisa herdar de motor::RuntimeSystem");
        return static_cast<T*>(FindSystem(typeid(T)));
    }

    // --- Animação (Animator.h) ---
    // Animator desta entidade ou do primeiro descendente que tiver um (ex.: o modelo filho do jogador), ou nullptr.
    Animator* GetAnimator();

    // --- Log com o nome da entidade: Log("vidas = {}", vidas) ---
    void Log(std::string_view message) {
        WriteLog(message, nullptr, 0);
    }
    template <class... Args>
    void Log(std::string_view fmt, const Args&... args) {
        const detail::LogArg list[] = {args...};
        WriteLog(fmt, list, sizeof...(Args));
    }

private:
    friend class SceneRuntime;
    void WriteLog(std::string_view fmt, const detail::LogArg* args, size_t count);
    bool InputEnabled() const;
    RuntimeSystem* FindSystem(const std::type_info& type);

    SceneRuntime* m_runtime = nullptr;
    EntityId m_entity = 0;
    bool m_started = false;
};

} // namespace motor
