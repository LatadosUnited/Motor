#include "EditorContext.h"
#include "drawers/FieldDrawers.h"

#include <imgui.h>

#include <string>

namespace editor {

namespace {
// Imagem do céu (.hdr do projeto). Devolve true se mudou.
bool SkyTextureCombo(EditorContext& ctx, motor::Environment& env) {
    std::string current;
    if (env.skyTexture) {
        if (const auto ref = ctx.assets.RefOf(env.skyTexture.get())) current = ref->name;
    }
    const auto chosen = AssetCombo(ctx, "Imagem do céu", motor::AssetType::EnvironmentMap,
                                   env.skyTexture ? &current : nullptr, "(nenhuma)");
    if (!chosen) return false;
    if (chosen->empty()) {
        env.skyTexture = nullptr;
        return true;
    }
    std::string error;
    auto image = ctx.assets.GetEnvironmentMap(*chosen, &error);
    if (!image) {
        ctx.pendingError = std::move(error);
        return false;
    }
    env.skyTexture = std::move(image);
    env.skyMode = motor::SkyMode::Texture;
    return true;
}
} // namespace

void DrawScenePanel(EditorContext& ctx) {
    if (!ImGui::Begin("Cena")) {
        ImGui::End();
        return;
    }

    motor::Environment& env = ctx.scene.environment;

    ImGui::SeparatorText("Céu");
    int mode = static_cast<int>(env.skyMode);
    const char* modes[] = {"Cor", "Imagem HDR"};
    if (ImGui::Combo("Tipo de céu", &mode, modes, 2)) env.skyMode = static_cast<motor::SkyMode>(mode);
    if (env.skyMode == motor::SkyMode::Color) {
        ImGui::ColorEdit3("Céu", &env.skyColor.x);
        ImGui::SetItemTooltip("Cor do horizonte (o zênite fica um pouco mais escuro); também é a cor da neblina");
    } else {
        SkyTextureCombo(ctx, env);
        ImGui::SetItemTooltip("Arquivo .hdr equirretangular do projeto (arraste do painel Projeto)");
        ImGui::SliderFloat("Rotação", &env.skyRotation, -180.0f, 180.0f, "%.0f°");
        if (!env.skyTexture) ImGui::TextDisabled("Sem imagem: o céu usa a cor.");
    }
    ImGui::DragFloat("Brilho do céu", &env.skyIntensity, 0.01f, 0.0f, 20.0f, "%.2f");
    ImGui::DragFloat("Luz do céu", &env.skyLight, 0.01f, 0.0f, 5.0f, "%.2f");
    ImGui::SetItemTooltip("Quanto o céu ilumina a cena (luz difusa e reflexos). 0 = só a luz ambiente");

    ImGui::SeparatorText("Ambiente");
    ImGui::SliderFloat("Neblina", &env.fogDensity, 0.0f, 0.08f, "%.4f");
    ImGui::ColorEdit3("Luz ambiente", &env.ambient.x);
    ImGui::SetItemTooltip("Luz extra sem direção que chega a todos os objetos (para uma cena noturna, use um tom escuro)");
    ImGui::DragFloat("Distância das sombras", &env.shadowDistance, 0.5f, 5.0f, 500.0f, "%.0f m");
    ImGui::SetItemTooltip("Até onde, a partir da câmera, a luz direcional projeta sombras");

    ImGui::SeparatorText("Pós-processamento");
    ImGui::DragFloat("Exposição", &env.exposure, 0.01f, 0.0f, 10.0f, "%.2f");
    int toneMapper = static_cast<int>(env.toneMapper);
    const char* toneMappers[] = {"ACES (filme)", "Reinhard", "Nenhum"};
    if (ImGui::Combo("Tone mapping", &toneMapper, toneMappers, 3)) env.toneMapper = static_cast<motor::ToneMapper>(toneMapper);
    ImGui::Checkbox("Bloom", &env.bloom);
    if (env.bloom) {
        ImGui::SliderFloat("Intensidade do bloom", &env.bloomIntensity, 0.0f, 3.0f, "%.2f");
        ImGui::SliderFloat("Limiar do bloom", &env.bloomThreshold, 0.0f, 5.0f, "%.2f");
        ImGui::SetItemTooltip("Brilho a partir do qual a luz \"vaza\" (1 = mais claro que o branco)");
    }
    ImGui::Checkbox("FXAA (suaviza serrilhado)", &env.fxaa);

    ImGui::SeparatorText("Info");
    ImGui::Text("Entidades: %zu", ctx.scene.Entities().size());
    const size_t lights = ctx.scene.CollectLights().size();
    ImGui::Text("Luzes ativas: %zu (máx. %zu)", lights, motor::kMaxLights);
    ImGui::TextDisabled("Luzes são entidades: menu Entidade > Luz direcional / pontual / spot.");

    ImGui::End();
}

} // namespace editor
