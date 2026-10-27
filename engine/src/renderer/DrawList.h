#pragma once

// Header privado do renderer: o que Submit/SubmitSkinned guardam para desenhar no EndScene (várias passadas
// usam a mesma lista: sombras e cena).

#include "motor/renderer/DrawOrder.h"
#include "motor/renderer/Material.h"
#include "motor/renderer/Mesh.h"
#include "motor/renderer/ShadowMath.h"

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace motor {

struct DrawItem {
    const Mesh* mesh = nullptr;
    const Material* material = nullptr;  // de quem chamou o Submit: vivo até o EndScene, sem cópia
    Aabb bounds;              // no mundo (malha com pele: a pose de repouso com folga)
    uint32_t jointOffset = 0; // em Renderer::m_joints
    uint32_t jointCount = 0;
    bool skinned = false;
    uint32_t materialId = 0;  // em Renderer::m_materialIds (dado no Submit, com o material ainda no cache)
    uint32_t shadowId = 0;    // em Renderer::m_shadowIds
    bool transparent = false;  // material->blend == Transparent, guardado no Submit (o material está longe na memória)
    ShadowCasting shadows = ShadowCasting::On;  // Off: fora dos mapas de sombra; ShadowsOnly: só neles
    bool retained = false;    // de um RenderWorld (Fase 28): caixa e matriz da normal já vieram prontas,
    bool cameraVisible = false;  // e o recorte saiu da grade: na câmera
    uint32_t shadowMask = 0;     // e nas passadas de sombra (um bit por passada do ShadowRenderer::Plan)
};

// Envia as juntas de um item com pele para o bloco Joints (binding 2) antes de desenhar.
using JointUploader = std::function<void(const DrawItem& item)>;

// Desenho em lote (instancing, Fase 23; pela GPU, Fase 28). Os itens do quadro vão uma vez para o buffer de objetos
// (Objects, binding 4: matriz de mundo e da normal, na posição do item em m_items). Cada passada grava a sua lista de
// índices de objetos (atributo 5 por instância, object.glsl) e um comando de desenho indireto por lote; lotes seguidos
// com o mesmo estado (shader, material, VAO) saem num glMultiDrawElementsIndirect só, mesmo com malhas diferentes.
struct DrawBatch {
    const DrawItem* first = nullptr;  // malha, material e pele do lote (todos iguais)
    uint32_t start = 0;               // na lista de índices da passada
    uint32_t count = 0;
    uint32_t firstIndex = 0;          // de `first` na lista de itens (a chave dele)
};

// Ordem das passadas de sombra: os itens ordenados pela chave de sombra (DrawKey.material = número em `params`).
struct ShadowOrder {
    std::span<const uint32_t> order;
    std::span<const DrawKey> keys;
    const ShadowParamsIds* params = nullptr;
};

class StreamBuffer;

class InstanceBuffers {
public:
    InstanceBuffers();
    ~InstanceBuffers();
    InstanceBuffers(const InstanceBuffers&) = delete;
    InstanceBuffers& operator=(const InstanceBuffers&) = delete;

    // Grava os objetos (2 matrizes por item: mundo e normal, montadas no Submit) e deixa o buffer no binding 4.
    void UploadObjects(std::span<const glm::mat4> objects);
    // Grava a lista de índices de uma passada e um comando por lote (a malha do primeiro item, `count` instâncias).
    void UploadPass(std::span<const uint32_t> indices, std::span<const DrawBatch> batches);
    // Os lotes [first, first + count) da última passada num draw só. Precisam ter o mesmo VAO (Mesh::Gpu).
    void Draw(size_t first, size_t count);
    // Fim da cena: o que foi gravado até aqui volta a ser usado quando a GPU passar (anel, StreamBuffer).
    void Fence();

private:
    std::unique_ptr<StreamBuffer> m_stream;
    uint64_t m_commands = 0;          // deslocamento dos comandos da última passada no buffer
    std::vector<unsigned> m_vaos;     // de cada lote da última passada
    uint64_t m_objectAlignment = 256; // GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT
};

} // namespace motor
