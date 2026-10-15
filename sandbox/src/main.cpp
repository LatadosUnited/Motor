#include <motor/Motor.h>

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <exception>
#include <vector>

using namespace motor;

namespace {

struct Object {
    Transform transform;
    std::shared_ptr<Mesh> mesh;
    Material material;
    glm::vec3 spinAxis{0.0f};
    float spinSpeed = 0.0f;  // rad/s
};

class Sandbox final : public Application {
public:
    Sandbox() : Application(MakeDesc()), m_cameraController(m_camera) {}

private:
    static ApplicationDesc MakeDesc() {
        ApplicationDesc desc;
        desc.window.title = "Motor Sandbox";
        return desc;
    }

    void OnInit() override {
        log::Info("Controles: botão direito + mouse = olhar | WASD/QE = mover | Shift = rápido | "
                  "F1 = wireframe | Esc = sair");

        auto cube = Mesh::CreateCube();
        auto sphere = Mesh::CreateSphere();
        auto ground = Mesh::CreatePlane(40.0f);

        // Chão quadriculado.
        Object floor;
        floor.mesh = ground;
        floor.transform.scale = glm::vec3(200.0f);
        floor.material.albedo = Texture2D::CreateChecker(256, 2, {210, 210, 215, 255}, {150, 155, 165, 255});
        floor.material.specular = 0.1f;
        m_objects.push_back(floor);

        // Grade de cubos e esferas coloridos.
        const glm::vec4 palette[] = {
            {0.95f, 0.35f, 0.30f, 1.0f}, {0.98f, 0.75f, 0.25f, 1.0f}, {0.35f, 0.80f, 0.45f, 1.0f},
            {0.30f, 0.60f, 0.95f, 1.0f}, {0.70f, 0.45f, 0.90f, 1.0f},
        };
        int i = 0;
        for (int z = -2; z <= 2; ++z) {
            for (int x = -2; x <= 2; ++x, ++i) {
                Object o;
                const bool isSphere = (x + z) % 2 != 0;
                o.mesh = isSphere ? sphere : cube;
                o.transform.position = {x * 2.5f, 0.6f, z * 2.5f - 4.0f};
                o.transform.scale = glm::vec3(isSphere ? 1.2f : 1.0f);
                o.material.color = palette[i % 5];
                o.material.shininess = isSphere ? 64.0f : 16.0f;
                o.spinAxis = glm::normalize(glm::vec3(0.3f * x, 1.0f, 0.3f * z));
                o.spinSpeed = isSphere ? 0.0f : 0.6f + 0.15f * static_cast<float>(i % 4);
                m_objects.push_back(o);
            }
        }

        m_camera.position = {0.0f, 4.0f, 9.0f};
        m_camera.LookAt({0.0f, 0.5f, -4.0f});
    }

    void OnFixedUpdate(float dt) override {
        for (auto& o : m_objects) {
            if (o.spinSpeed != 0.0f) o.transform.Rotate(o.spinAxis, o.spinSpeed * dt);
        }
    }

    void OnUpdate(float dt) override {
        if (Input::WasKeyPressed(Key::Escape)) Quit();
        if (Input::WasKeyPressed(Key::F1)) GetRenderer().SetWireframe(!GetRenderer().IsWireframe());

        m_cameraController.Update(dt, GetWindow());

        // Sol girando lentamente.
        const float t = static_cast<float>(ElapsedTime()) * 0.2f;
        m_light.direction = {std::cos(t) * 0.6f, -1.0f, std::sin(t) * 0.6f};
    }

    void OnRender(Renderer& renderer) override {
        renderer.BeginScene(m_camera, m_light);
        for (const auto& o : m_objects) {
            renderer.Submit(*o.mesh, o.material, o.transform.Matrix());
        }
        renderer.EndScene();
    }

    Camera m_camera;
    FlyCameraController m_cameraController;
    DirectionalLight m_light;
    std::vector<Object> m_objects;
};

} // namespace

int main() {
    try {
        Sandbox app;
        app.Run();
    } catch (const std::exception& e) {
        log::Error("Erro fatal: {}", e.what());
        return 1;
    }
    return 0;
}
