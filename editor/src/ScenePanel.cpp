#include "EditorContext.h"

#include <imgui.h>

namespace editor {

void DrawScenePanel(EditorContext& ctx) {
    if (!ImGui::Begin("Cena")) {
        ImGui::End();
        return;
    }

    motor::Environment& env = ctx.scene.environment;

    ImGui::SeparatorText("Ambiente");
    ImGui::ColorEdit3("Céu", &env.skyColor.x);
    ImGui::SliderFloat("Neblina", &env.fogDensity, 0.0f, 0.08f, "%.4f");
    ImGui::ColorEdit3("Luz ambiente", &env.ambient.x);
    ImGui::SetItemTooltip("Luz sem direção que chega a todos os objetos (para uma cena noturna, use um tom escuro)");

    ImGui::SeparatorText("Info");
    ImGui::Text("Entidades: %zu", ctx.scene.Entities().size());
    const size_t lights = ctx.scene.CollectLights().size();
    ImGui::Text("Luzes ativas: %zu (máx. %zu)", lights, motor::kMaxLights);
    ImGui::TextDisabled("Luzes são entidades: menu Entidade > Luz direcional / pontual / spot.");

    ImGui::End();
}

} // namespace editor
