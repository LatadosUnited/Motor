#pragma once

#include "motor/renderer/Material.h"

#include <memory>

namespace motor {

class Mesh;

// Desenha uma malha com um material.
struct MeshRenderer {
    std::shared_ptr<Mesh> mesh;
    Material material;
};

} // namespace motor
