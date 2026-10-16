#include "motor/scene/MeshRenderer.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/scene/Component.h"

namespace motor {

MOTOR_COMPONENT_MODULE(MeshRenderer) {
    // A ordem dos campos é a do Inspector e a do arquivo.
    registry.Register<MeshRenderer>("MeshRenderer", "Mesh Renderer")
        .Field("mesh", "Malha", &MeshRenderer::mesh)
        .FieldRef("texture", "Textura", [](MeshRenderer& m) -> auto& { return m.material.albedo; })
        .FieldRef("color", "Cor", [](MeshRenderer& m) -> auto& { return m.material.color; }, {.color = true})
        .FieldRef("specular", "Especular", [](MeshRenderer& m) -> auto& { return m.material.specular; },
                  {.slider = true, .min = 0.0f, .max = 2.0f})
        .FieldRef("shininess", "Brilho", [](MeshRenderer& m) -> auto& { return m.material.shininess; },
                  {.slider = true, .logarithmic = true, .min = 1.0f, .max = 256.0f, .format = "%.0f"})
        .OnAdd([](MeshRenderer& m, AssetLibrary& assets) { m.mesh = assets.FindMesh("Cubo"); });
}

} // namespace motor
