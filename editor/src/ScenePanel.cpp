#include "EditorContext.h"

#include <imgui.h>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>

namespace editor {

void DrawScenePanel(EditorContext& ctx) {
    if (!ImGui::Begin("Cena")) {
        ImGui::End();
        return;
    }

    motor::DirectionalLight& light = ctx.scene.light;
    motor::Environment& env = ctx.scene.environment;

    ImGui::SeparatorText("Luz direcional");
    {
        // A direção é editada como ângulos do "sol": elevação acima do horizonte e azimute.
        const glm::vec3 toSun = -glm::normalize(light.direction);
        float elevation = glm::degrees(std::asin(glm::clamp(toSun.y, -1.0f, 1.0f)));
        float azimuth = glm::degrees(std::atan2(toSun.z, toSun.x));

        bool changed = ImGui::SliderFloat("Elevação", &elevation, 1.0f, 89.0f, "%.0f°");
        changed |= ImGui::SliderFloat("Azimute", &azimuth, -180.0f, 180.0f, "%.0f°");
        if (changed) {
            const float el = glm::radians(elevation);
            const float az = glm::radians(azimuth);
            light.direction = -glm::vec3(std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az));
        }
    }
    ImGui::ColorEdit3("Cor", &light.color.x);
    ImGui::DragFloat("Intensidade", &light.intensity, 0.01f, 0.0f, 10.0f, "%.2f");
    ImGui::ColorEdit3("Ambiente", &light.ambient.x);

    ImGui::SeparatorText("Ambiente");
    ImGui::ColorEdit3("Céu", &env.skyColor.x);
    ImGui::SliderFloat("Neblina", &env.fogDensity, 0.0f, 0.08f, "%.4f");

    ImGui::SeparatorText("Info");
    ImGui::Text("Entidades: %zu", ctx.scene.Entities().size());

    ImGui::End();
}

} // namespace editor
