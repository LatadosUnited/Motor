#pragma once

#include "motor/renderer/Material.h"
#include "motor/scene/Component.h"

#include <memory>

namespace motor {

class Mesh;

// Desenha uma malha com um material.
//
// O material pode ser um asset (.mmat, `sharedMaterial`), compartilhado por todos os objetos que usam o mesmo
// arquivo: mudar um campo dele muda todos. Sem asset, vale o material próprio do componente (`material`),
// que é como as cenas antigas guardavam cor e textura. Com asset, `material` fica guardado mas não é usado.
struct MeshRenderer {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> sharedMaterial;  // nulo = usa `material`
    Material material;

    const Material& Resolved() const { return sharedMaterial ? *sharedMaterial : material; }
};

// Campo "material" do Mesh Renderer: referência a um .mmat pelo GUID (com o caminho junto, como plano B).
template <> struct FieldTraits<std::shared_ptr<Material>> {
    static CustomFieldType Describe();
};

} // namespace motor
