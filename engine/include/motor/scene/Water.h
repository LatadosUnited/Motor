#pragma once

// Água (Fase 28, Tarefa 13): um retângulo de água de `size` metros centrado na entidade (plano XZ dela), com as
// ondas, o reflexo do céu, a refração e a cor pela profundidade do motor::WaterSurface. A cena manda para o
// Renderer::SubmitWater (Scene::SubmitTo); a malha (uma grade de células de ~1 m) é refeita só quando o tamanho muda.
// Sem física: a água não segura nada nem aparece nos raios.

#include "motor/renderer/Water.h"
#include "motor/scene/Component.h"

#include <glm/vec2.hpp>

#include <memory>

namespace motor {

struct Water {
    glm::vec2 size{100.0f, 100.0f};
    WaterSurface surface;

    // Malha do tamanho atual (refeita quando `size` muda). Fora dos campos: não vai para a cena.
    const Mesh& GridMesh() const;

    // Cache do GridMesh (públicos para o tipo continuar de layout padrão, como os outros componentes).
    mutable std::shared_ptr<Mesh> cachedMesh;
    mutable glm::vec2 cachedMeshSize{0.0f};
};

} // namespace motor
