#pragma once

#include "motor/renderer/Texture.h"

#include <glm/vec4.hpp>

#include <memory>

namespace motor {

struct Material {
    glm::vec4 color{1.0f};                // multiplicado pelo albedo (espaço sRGB)
    std::shared_ptr<Texture2D> albedo;    // nulo => branco
    float specular = 0.5f;
    float shininess = 32.0f;
};

} // namespace motor
