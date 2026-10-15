#pragma once

// Componentes que vêm com a engine. Para criar outros, veja Component.h.

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
