#include "motor/scene/Water.h"

#include "motor/renderer/Mesh.h"

namespace motor {

const Mesh& Water::GridMesh() const {
    if (!cachedMesh || cachedMeshSize != size) {
        cachedMesh = CreateWaterGrid(size);
        cachedMeshSize = size;
    }
    return *cachedMesh;
}

MOTOR_COMPONENT_MODULE(Water) {
    registry.Register<Water>("Water", "Água")
        .Field("size", "Tamanho", &Water::size, {.speed = 0.5f, .min = 0.1f, .max = 100000.0f,
                                                 .tooltip = "Largura (X) e comprimento (Z) em metros, centrado na entidade"})
        .FieldRef("shallowColor", "Cor rasa", [](Water& w) -> auto& { return w.surface.shallowColor; }, {.color = true})
        .FieldRef("deepColor", "Cor funda", [](Water& w) -> auto& { return w.surface.deepColor; }, {.color = true})
        .FieldRef("depthFade", "Transparência", [](Water& w) -> auto& { return w.surface.depthFade; },
                  {.speed = 0.05f, .min = 0.05f, .max = 200.0f, .format = "%.2f m",
                   .tooltip = "Metros de água até o fundo quase sumir na cor funda"})
        .FieldRef("waveHeight", "Altura das ondas", [](Water& w) -> auto& { return w.surface.waveHeight; },
                  {.speed = 0.01f, .min = 0.0f, .max = 20.0f, .format = "%.2f m"})
        .FieldRef("waveLength", "Comprimento das ondas", [](Water& w) -> auto& { return w.surface.waveLength; },
                  {.speed = 0.1f, .min = 0.5f, .max = 500.0f, .format = "%.1f m"})
        .FieldRef("waveSpeed", "Velocidade das ondas", [](Water& w) -> auto& { return w.surface.waveSpeed; },
                  {.speed = 0.01f, .min = 0.0f, .max = 10.0f})
        .FieldRef("waveDirection", "Direção das ondas", [](Water& w) -> auto& { return w.surface.waveDirection; },
                  {.slider = true, .min = -180.0f, .max = 180.0f, .format = "%.0f°", .tooltip = "0° = para +X"})
        .FieldRef("reflection", "Reflexo", [](Water& w) -> auto& { return w.surface.reflection; },
                  {.slider = true, .min = 0.0f, .max = 1.0f, .tooltip = "Força do reflexo do céu"})
        .FieldRef("refraction", "Refração", [](Water& w) -> auto& { return w.surface.refraction; },
                  {.slider = true, .min = 0.0f, .max = 0.2f, .tooltip = "Quanto as ondas entortam o fundo"})
        .FieldRef("foam", "Espuma", [](Water& w) -> auto& { return w.surface.foam; },
                  {.speed = 0.01f, .min = 0.0f, .max = 10.0f, .format = "%.2f m",
                   .tooltip = "Largura da espuma onde a água encontra o chão ou objetos"});
}

} // namespace motor
