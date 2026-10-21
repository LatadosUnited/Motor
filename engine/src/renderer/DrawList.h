#pragma once

// Header privado do renderer: o que Submit/SubmitSkinned guardam para desenhar no EndScene (várias passadas
// usam a mesma lista: sombras e cena).

#include "motor/renderer/Material.h"
#include "motor/renderer/Mesh.h"
#include "motor/renderer/ShadowMath.h"

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace motor {

struct DrawItem {
    const Mesh* mesh = nullptr;
    const Material* material = nullptr;  // de quem chamou o Submit: vivo até o EndScene, sem cópia
    glm::mat4 model{1.0f};
    Aabb bounds;              // no mundo (malha com pele: a pose de repouso com folga)
    uint32_t jointOffset = 0; // em Renderer::m_joints
    uint32_t jointCount = 0;
    bool skinned = false;
    ShadowCasting shadows = ShadowCasting::On;  // Off: fora dos mapas de sombra; ShadowsOnly: só neles
};

// Envia as juntas de um item com pele para o bloco Joints (binding 2) antes de desenhar.
using JointUploader = std::function<void(const DrawItem& item)>;

// Desenho em lote (instancing, Fase 23). Os itens do quadro vão uma vez para o buffer de objetos (Objects, binding
// 4: matriz de mundo e da normal, na posição do item em m_items); cada passada grava a sua lista de índices
// (DrawIndices, binding 5) e desenha cada lote com um draw instanciado a partir de `start` (uDrawBase, object.glsl).
struct DrawBatch {
    const DrawItem* first = nullptr;  // malha, material e pele do lote (todos iguais)
    uint32_t start = 0;               // na lista de índices da passada
    uint32_t count = 0;
};

class InstanceBuffers {
public:
    InstanceBuffers();
    ~InstanceBuffers();
    InstanceBuffers(const InstanceBuffers&) = delete;
    InstanceBuffers& operator=(const InstanceBuffers&) = delete;

    // Grava os objetos de todos os itens (uma vez por cena) e deixa o buffer no binding 4.
    void UploadObjects(std::span<const DrawItem> items);
    // Grava a lista de índices de uma passada e deixa o buffer no binding 5.
    void UploadIndices(std::span<const uint32_t> indices);

private:
    unsigned int m_objects = 0;
    unsigned int m_indices = 0;
    size_t m_objectBytes = 0;
    size_t m_indexBytes = 0;
    std::vector<glm::mat4> m_scratch;  // 2 matrizes por item, reaproveitado entre quadros
};

} // namespace motor
