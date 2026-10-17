#pragma once

// Header privado do renderer: o que Submit/SubmitSkinned guardam para desenhar no EndScene (várias passadas
// usam a mesma lista: sombras e cena).

#include "motor/renderer/Material.h"
#include "motor/renderer/Mesh.h"
#include "motor/renderer/ShadowMath.h"

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <functional>

namespace motor {

struct DrawItem {
    const Mesh* mesh = nullptr;
    Material material;
    glm::mat4 model{1.0f};
    Aabb bounds;              // no mundo (malha com pele: a pose de repouso com folga)
    uint32_t jointOffset = 0; // em Renderer::m_joints
    uint32_t jointCount = 0;
    bool skinned = false;
};

// Envia as juntas de um item com pele para o bloco Joints (binding 2) antes de desenhar.
using JointUploader = std::function<void(const DrawItem& item)>;

} // namespace motor
