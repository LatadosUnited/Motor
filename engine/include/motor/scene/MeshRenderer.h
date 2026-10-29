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
    // Sombra: Com sombra (padrão), Sem sombra, ou Só sombra (some da imagem e continua fazendo sombra: o corpo
    // em primeira pessoa; o Animator e os scripts seguem rodando, ao contrário de desativar a entidade).
    ShadowCasting shadows = ShadowCasting::On;
    // Estático (Fase 28): o objeto não se mexe no jogo. O renderer o guarda entre quadros (Scene::SubmitTo) em vez
    // de recebê-lo de novo a cada um. No jogo, mudar um estático por script (posição, malha, material) só aparece
    // depois de Scene::MarkRenderDirty; no editor, as edições aparecem sozinhas.
    bool isStatic = false;

    const Material& Resolved() const { return sharedMaterial ? *sharedMaterial : material; }
};

// Campo "material" do Mesh Renderer: referência a um .mmat pelo GUID (com o caminho junto, como plano B).
template <> struct FieldTraits<std::shared_ptr<Material>> {
    static CustomFieldType Describe();
};

// Sombra do Mesh Renderer: na cena, o nome ("On", "Off", "ShadowsOnly").
template <> struct FieldTraits<ShadowCasting> {
    static CustomFieldType Describe();
};

// Modo de alfa do material: na cena e no .mmat, o nome ("Opaque", "Cutout", "Transparent").
template <> struct FieldTraits<BlendMode> {
    static CustomFieldType Describe();
};

} // namespace motor
