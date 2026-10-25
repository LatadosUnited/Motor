#pragma once

#include "motor/renderer/Camera.h"
#include "motor/renderer/DrawOrder.h"
#include "motor/renderer/Environment.h"
#include "motor/renderer/Light.h"
#include "motor/renderer/Material.h"
#include "motor/renderer/Mesh.h"
#include "motor/renderer/Shader.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace motor {

class Framebuffer;
class GpuTimers;
class InstanceBuffers;
class PostProcess;
class ShadowRenderer;
class SkyRenderer;
struct DrawItem;

struct RenderStats {
    uint32_t drawCalls = 0;        // malhas desenhadas em todas as passadas (cena, sombras, contornos)
    uint32_t triangles = 0;        // da passada da cena
    uint32_t transparent = 0;      // itens transparentes desenhados (depois do céu, de trás para frente)
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
    int Samples() const { return m_samples; }
    // Sombras (opções do jogador): desligadas em todas as luzes, ou a qualidade de cada luz limitada a `maxQuality`.
    void SetShadows(bool enabled, ShadowQuality maxQuality = ShadowQuality::High) {
        m_shadowsEnabled = enabled;
        m_maxShadowQuality = maxQuality;
    }
    // Multiplica o tamanho na tela usado para escolher o nível de LOD (Mesh::Lods): acima de 1 o detalhe dura mais
    // longe, abaixo de 1 os níveis simples entram mais perto (qualidade baixa). Vale a partir do próximo BeginScene.
    void SetLodBias(float bias) { m_lodBias = std::max(bias, 0.01f); }

    // target == nullptr desenha na janela. Até kMaxLights luzes (as demais são ignoradas, com aviso no log);
    // nenhuma = só a luz do céu e a ambiente.
    void BeginScene(const Camera& camera, std::span<const Light> lights, const Environment& environment = {},
                    const Framebuffer* target = nullptr);
    // A malha e o material são guardados por referência (sem cópia): precisam continuar vivos e sem mudar até o
    // EndScene.
    void Submit(const Mesh& mesh, const Material& material, const glm::mat4& model,
                ShadowCasting shadows = ShadowCasting::On);
    // Linhas sem iluminação por cima da imagem final (só aparecem onde não há nada na frente).
    void SubmitWireframe(const Mesh& mesh, const glm::mat4& model, const glm::vec4& color);
    // Malha com pele (animação): `joints` são as matrizes das juntas da pele (Skin::JointMatrices), no espaço
    // de `model`. Até kMaxJoints (as demais ficam paradas, com aviso no log). Malha e material como no Submit.
    void SubmitSkinned(const Mesh& mesh, const Material& material, const glm::mat4& model,
                       std::span<const glm::mat4> joints, ShadowCasting shadows = ShadowCasting::On);
    void EndScene();

    const RenderStats& GetStats() const { return m_stats; }

    // Resultado da última cena (válido até a próxima), base para efeitos de ambiente (água, partículas):
    // Com MSAA, a profundidade só é copiada para a textura nos quadros com wireframes ou com KeepSceneDepth(true):
    // a cópia do depth buffer com amostras custa caro na GPU integrada (~5 ms a 1280x720 com MSAA 4x).
    void KeepSceneDepth(bool keep) { m_keepSceneDepth = keep; }
    unsigned int SceneDepthTexture() const;  // DEPTH_COMPONENT32F, valores do depth buffer (0 a 1)
    unsigned int SceneColorTexture() const;  // R11G11B10F (sem alfa), luz linear antes do pós-processamento
    unsigned int SkyCubemap() const;         // céu (RGBA16F linear, com mips), sem a rotação e o brilho
    unsigned int ReflectionCubemap() const;  // céu pré-filtrado: mip = rugosidade * (níveis - 1)

private:
    struct WireItem {
        const Mesh* mesh;
        glm::mat4 model;
        glm::vec4 color;
    };
    // Posições dos uniforms por desenho do lit.frag, resolvidas uma vez por shader (lit e com pele).
    struct LitUniforms {
        int alphaMode = -1, drawBase = -1, color = -1, pbr = -1, emissive = -1;
    };

    void DrawScene();
    void DrawWireframes();
    void BindTarget() const;
    void UploadJoints(const DrawItem& item);
    DrawItem& AddItem(const Mesh& mesh, const Material& material, const glm::mat4& model, ShadowCasting shadows);

    std::unique_ptr<Shader> m_litShader;
    std::unique_ptr<Shader> m_skinnedShader;  // skinned.vert + lit.frag
    std::unique_ptr<Shader> m_unlitShader;
    LitUniforms m_litUniforms, m_skinnedUniforms;
    int m_unlitModel = -1, m_unlitColor = -1;
    std::shared_ptr<Texture2D> m_whiteTexture;
    std::unique_ptr<PostProcess> m_post;
    std::unique_ptr<ShadowRenderer> m_shadows;
    std::unique_ptr<SkyRenderer> m_sky;
    std::unique_ptr<InstanceBuffers> m_instances;  // objetos e índices dos desenhos em lote (Fase 23)
    std::unique_ptr<GpuTimers> m_gpuTimers;  // tempo de GPU por passada (contadores, Fase 22)
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

    // Ordem de desenho do quadro (Fase 23): número de material de cada item e as ordens da cena e das sombras.
    MaterialIds m_materialIds;
    ShadowParamsIds m_shadowIds;
    std::vector<DrawKey> m_sceneKeys, m_shadowKeys;
    std::vector<DrawKey> m_lastSceneKeys, m_lastShadowKeys;  // do quadro anterior: iguais, a ordem não muda
    std::vector<glm::mat4> m_objects;  // 2 por item (mundo e normal), na ordem de m_items: o buffer de objetos
    // Material do Submit anterior: o mesmo objeto, ou os mesmos valores, não consultam as tabelas.
    const Material* m_lastMaterial = nullptr;
    MaterialKey m_lastMaterialKey;
    ShadowParams m_lastShadowParams;
    uint32_t m_lastMaterialId = 0, m_lastShadowId = 0;
    std::vector<uint32_t> m_sceneOrder, m_shadowOrder;

    glm::vec4 m_clearColor{0.08f, 0.09f, 0.10f, 1.0f};
    int m_windowWidth = 0;
    int m_windowHeight = 0;
    int m_samples = 4;
    bool m_keepSceneDepth = false;
    float m_lodBias = 1.0f;
    float m_lodScale = 1.0f;  // m_lodBias / (2 tan(fov / 2)) da câmera do quadro
    bool m_shadowsEnabled = true;
    ShadowQuality m_maxShadowQuality = ShadowQuality::High;
    const Framebuffer* m_target = nullptr;
    bool m_wireframe = false;
    bool m_inScene = false;
    RenderStats m_stats;
};

} // namespace motor
