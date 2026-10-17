#pragma once

#include "motor/renderer/Camera.h"
#include "motor/renderer/Environment.h"
#include "motor/renderer/Light.h"
#include "motor/renderer/Material.h"
#include "motor/renderer/Mesh.h"
#include "motor/renderer/Shader.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace motor {

class Framebuffer;
class PostProcess;
class ShadowRenderer;
class SkyRenderer;
struct DrawItem;

struct RenderStats {
    uint32_t drawCalls = 0;        // malhas desenhadas em todas as passadas (cena, sombras, contornos)
    uint32_t triangles = 0;        // da passada da cena
    uint32_t shadowPasses = 0;     // mapas de sombra desenhados (cada cascata e cada face de cubo)
    uint32_t shadowDrawCalls = 0;  // parte dos drawCalls que foi para as sombras
    uint32_t culled = 0;           // objetos fora da câmera (não desenhados na cena)
};

// Desenha a cena com PBR, sombras, céu e pós-processamento (Fase 7).
//
// Submit* só guardam o pedido: tudo é desenhado no EndScene, em passadas: mapas de sombra -> cena em HDR
// (com MSAA) e o céu ao fundo -> bloom, tone mapping e FXAA no alvo -> contornos do editor por cima.
class Renderer {
public:
    static constexpr size_t kMaxJoints = 128;  // mesmo valor do skinned.vert

    Renderer();  // exige um contexto OpenGL atual (criado pela Window); carrega as funções do OpenGL
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Chamado pela Application no início de cada frame: limpa a janela.
    void BeginFrame(int width, int height);
    void SetClearColor(const glm::vec4& color) { m_clearColor = color; }

    void SetWireframe(bool enabled) { m_wireframe = enabled; }
    bool IsWireframe() const { return m_wireframe; }
    // Amostras de MSAA da cena (1 = sem). Padrão: 4.
    void SetSamples(int samples) { m_samples = samples < 1 ? 1 : samples; }

    // target == nullptr desenha na janela. Até kMaxLights luzes (as demais são ignoradas, com aviso no log);
    // nenhuma = só a luz do céu e a ambiente.
    void BeginScene(const Camera& camera, std::span<const Light> lights, const Environment& environment = {},
                    const Framebuffer* target = nullptr);
    // O material é copiado; a malha precisa continuar viva até o EndScene.
    void Submit(const Mesh& mesh, const Material& material, const glm::mat4& model);
    // Linhas sem iluminação por cima da imagem final (só aparecem onde não há nada na frente).
    void SubmitWireframe(const Mesh& mesh, const glm::mat4& model, const glm::vec4& color);
    // Malha com pele (animação): `joints` são as matrizes das juntas da pele (Skin::JointMatrices), no espaço
    // de `model`. Até kMaxJoints (as demais ficam paradas, com aviso no log).
    void SubmitSkinned(const Mesh& mesh, const Material& material, const glm::mat4& model,
                       std::span<const glm::mat4> joints);
    void EndScene();

    const RenderStats& GetStats() const { return m_stats; }

    // Resultado da última cena (válido até a próxima), base para efeitos de ambiente (água, partículas):
    unsigned int SceneDepthTexture() const;  // DEPTH_COMPONENT32F, valores do depth buffer (0 a 1)
    unsigned int SceneColorTexture() const;  // RGBA16F, luz linear antes do pós-processamento
    unsigned int SkyCubemap() const;         // céu (RGBA16F linear, com mips), sem a rotação e o brilho
    unsigned int ReflectionCubemap() const;  // céu pré-filtrado: mip = rugosidade * (níveis - 1)

private:
    struct WireItem {
        const Mesh* mesh;
        glm::mat4 model;
        glm::vec4 color;
    };

    void DrawScene();
    void DrawWireframes();
    void BindTarget() const;
    void UploadJoints(const DrawItem& item);

    std::unique_ptr<Shader> m_litShader;
    std::unique_ptr<Shader> m_skinnedShader;  // skinned.vert + lit.frag
    std::unique_ptr<Shader> m_unlitShader;
    std::shared_ptr<Texture2D> m_whiteTexture;
    std::unique_ptr<PostProcess> m_post;
    std::unique_ptr<ShadowRenderer> m_shadows;
    std::unique_ptr<SkyRenderer> m_sky;
    unsigned int m_frameUbo = 0;
    unsigned int m_lightsUbo = 0;
    unsigned int m_shadowsUbo = 0;
    unsigned int m_jointsUbo = 0;
    bool m_warnedTooManyLights = false;
    bool m_warnedTooManyJoints = false;

    // Pedidos da cena atual (BeginScene..EndScene).
    Camera m_camera;
    std::vector<Light> m_lights;
    Environment m_environment;
    std::vector<DrawItem> m_items;
    std::vector<glm::mat4> m_joints;
    std::vector<WireItem> m_wires;

    glm::vec4 m_clearColor{0.08f, 0.09f, 0.10f, 1.0f};
    int m_windowWidth = 0;
    int m_windowHeight = 0;
    int m_samples = 4;
    const Framebuffer* m_target = nullptr;
    bool m_wireframe = false;
    bool m_inScene = false;
    RenderStats m_stats;
};

} // namespace motor
