// Gira a entidade continuamente. Script de exemplo do Motor: edite à vontade.
//
// Um script é uma struct que herda de motor::Script. Os campos registrados no bloco MOTOR_SCRIPT aparecem
// no Inspector e são salvos na cena; OnStart/OnUpdate/OnFixedUpdate/OnDestroy só rodam no modo Play.
// Ao salvar este arquivo o editor recompila os scripts e recarrega sem fechar.

#include <motor/scene/ScriptModule.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

struct Girar : motor::Script {
    glm::vec3 eixo{0.0f, 1.0f, 0.0f};
    float velocidade = 90.0f;  // graus por segundo
    bool local = true;         // eixo do próprio objeto (senão, do mundo)

    void OnUpdate(float dt) override {
        if (glm::length(eixo) < 1e-6f) return;
        const glm::quat delta = glm::angleAxis(glm::radians(velocidade * dt), glm::normalize(eixo));
        motor::Transform& t = GetTransform();
        t.rotation = glm::normalize(local ? t.rotation * delta : delta * t.rotation);
    }
};

MOTOR_SCRIPT(Girar) {
    registry.Register<Girar>("Girar", "Girar")
        .Field("eixo", "Eixo", &Girar::eixo)
        .Field("velocidade", "Velocidade", &Girar::velocidade,
               {.speed = 1.0f, .format = "%.0f°/s", .tooltip = "Graus por segundo (negativo gira ao contrário)"})
        .Field("local", "Eixo local", &Girar::local, {.tooltip = "Gira no eixo do próprio objeto, não do mundo"});
}
