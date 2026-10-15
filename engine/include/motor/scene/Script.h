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
#include "motor/core/Log.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace motor {

struct Entity;
struct Transform;
class Scene;
class SceneRuntime;
using EntityId = uint32_t;

// Tempo do jogo (não conta o tempo em pausa).
struct GameTime {
    float delta = 0.0f;       // segundos desde o último OnUpdate
    float fixedDelta = 0.0f;  // passo do OnFixedUpdate
    double elapsed = 0.0;     // segundos desde o Play
    uint64_t frame = 0;       // quantos OnUpdate já rodaram
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
    void Destroy(EntityId id);  // no fim do frame (com os descendentes); OnDestroy é chamado antes
    void DestroySelf();

    // --- Tempo e entrada ---
    const GameTime& Time() const;
    // Iguais aos de motor::Input, mas falsos/zero quando o jogo não tem o foco (ex.: digitando no editor).
    bool IsKeyDown(Key key) const;
    bool WasKeyPressed(Key key) const;
    bool IsMouseDown(MouseButton button) const;
    glm::vec2 MouseDelta() const;

    // --- Log com o nome da entidade: Log("vidas = {}", vidas) ---
    template <class... Args>
    void Log(std::format_string<Args...> fmt, Args&&... args) {
        WriteLog(std::format(fmt, std::forward<Args>(args)...));
    }

private:
    friend class SceneRuntime;
    void WriteLog(const std::string& message);
    bool InputEnabled() const;

    SceneRuntime* m_runtime = nullptr;
    EntityId m_entity = 0;
    bool m_started = false;
};

} // namespace motor
