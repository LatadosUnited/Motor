// Comparação com a Unity (bench/README.md), cena "scripts1000": cada cubo sobe e desce com um seno.
// Equivalente ao Bobber.cs do projeto gêmeo da Unity.
#include <motor/scene/ScriptModule.h>

#include <cmath>

struct Bobber : motor::Script {
    void OnStart() override {
        m_base = GetEntity().transform.position;
        m_fase = m_base.x * 0.1f + m_base.z * 0.07f;
    }

    void OnUpdate(float) override {
        auto& t = GetEntity().transform;
        t.position.y = m_base.y + std::sin(static_cast<float>(Time().elapsed) * 2.0f + m_fase) * 0.5f;
    }

private:
    glm::vec3 m_base{0.0f};
    float m_fase = 0.0f;
};

MOTOR_SCRIPT(Bobber) {
    registry.Register<Bobber>("Bobber", "Bobber");
}
