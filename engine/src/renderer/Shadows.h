#pragma once

// Header privado do renderer. Mapas de sombra: cascatas da luz direcional (textura array) e cubos das
// pontuais/spots (cubemap array, distância até a luz).

#include "motor/renderer/Camera.h"
#include "motor/renderer/Environment.h"
#include "motor/renderer/Light.h"
#include "motor/renderer/Shader.h"
#include "renderer/DrawList.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <span>
#include <string_view>
#include <string>
#include <vector>

namespace motor {

struct RenderStats;

class ShadowRenderer {
public:
    static constexpr int kCascades = 4;

    // Espelha o bloco "Shadows" (std140) do lit.frag.
    struct Data {
        glm::mat4 cascadeMatrices[kCascades]{};
        glm::vec4 cascadeSplits{0.0f};
        glm::vec4 cascadeTexels{0.0f};
        glm::vec4 params{0.0f};  // x = nº de cascatas, y = 1 / resolução das cascatas, z = 1 / resolução dos cubos, w = distância
    };

    struct Result {
        Data data;
        int cascadeLight = -1;            // índice (em `lights`) da direcional com cascatas
        ShadowQuality cascadeQuality = ShadowQuality::High;  // da direcional: filtro das cascatas no lit.frag
        std::vector<int> cubeLayer;       // por luz: camada do cubo de sombra, -1 = sem
    };

    explicit ShadowRenderer(const std::filesystem::path& shaderDir);
    ~ShadowRenderer();

    ShadowRenderer(const ShadowRenderer&) = delete;
    ShadowRenderer& operator=(const ShadowRenderer&) = delete;

    // Escolhe as luzes com sombra e desenha os mapas. Deixa o framebuffer próprio vinculado.
    Result Render(const Camera& camera, std::span<const Light> lights, const Environment& environment,
                  std::span<const DrawItem> items, InstanceBuffers& instances, const ShadowOrder& order,
                  const JointUploader& uploadJoints, RenderStats& stats);

    unsigned int CascadeTexture() const { return m_cascades; }
    unsigned int CubeTexture() const { return m_cubes; }

private:
    void EnsureCascades(int resolution);
    void EnsureCubes(int resolution);
    // Uma passada de sombra: uma cascata ou uma face de cubo. As listas de desenho de todas as passadas são montadas
    // em paralelo (Fase 26) e desenhadas depois, em ordem, na thread do OpenGL.
    struct Pass {
        glm::mat4 viewProjection{1.0f};
        Frustum frustum;
        bool point = false;
        bool skip = false;  // só limpar (face do cubo fora do cone do spot)
        glm::vec4 positionRange{0.0f};
        unsigned int texture = 0;
        int layer = 0;
        int resolution = 1;
        std::vector<uint32_t> indices;  // reaproveitados entre quadros
        std::vector<DrawBatch> batches;
    };
    void BuildPasses(std::span<const DrawItem> items, const ShadowOrder& order);
    void DrawPass(const Pass& pass, InstanceBuffers& instances, const ShadowOrder& order,
                  const JointUploader& uploadJoints, RenderStats& stats);

    // Posições dos uniforms do shadow.vert/.frag, resolvidas uma vez por variante.
    struct Uniforms {
        int lightViewProjection = -1, lightPositionRange = -1, drawBase = -1, alphaCutoff = -1, colorAlpha = -1;
    };
    struct Variant {
        std::unique_ptr<Shader> shader;
        Uniforms uniforms;
    };
    static Variant MakeVariant(const std::filesystem::path& dir, std::string_view defines);

    Variant m_directional, m_directionalSkinned, m_point, m_pointSkinned;
    std::vector<Pass> m_passes;     // as do quadro: as primeiras m_passCount valem
    size_t m_passCount = 0;
    std::vector<uint32_t> m_masks;  // por item: um bit por passada em que ele aparece
    unsigned int m_fbo = 0;
    unsigned int m_cascades = 0;
    int m_cascadeResolution = 0;
    // Cascadas distantes reaproveitadas (Fase 24): o que cada camada do mapa tem desenhado agora.
    Cascade m_drawnCascades[kCascades]{};
    glm::vec3 m_drawnDirection{0.0f};
    int m_drawnCount = 0;
    uint64_t m_frame = 0;
    unsigned int m_cubes = 0;
    int m_cubeResolution = 0;
};

} // namespace motor
