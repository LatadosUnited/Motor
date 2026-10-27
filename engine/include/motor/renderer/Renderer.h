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
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace motor {

class Framebuffer;
class GpuTimers;
class RenderWorld;
class InstanceBuffers;
class PostProcess;
class ShadowRenderer;
class SkyRenderer;
struct DrawBatch;
struct DrawItem;
struct Frustum;

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

    // Muitos Submit de uma vez, montados em paralelo (Fase 26): `fill(i, pedido)` para i em [0, count) preenche o
    // pedido ou devolve false para pular o i. Roda em várias threads ao mesmo tempo: só pode ler dados que ninguém
    // muda durante a chamada e escrever no que é só do i. O resultado é o mesmo de chamar Submit na ordem dos i.
    struct SubmitRequest {
        const Mesh* mesh = nullptr;
        const Material* material = nullptr;
        glm::mat4 model{1.0f};
        ShadowCasting shadows = ShadowCasting::On;
    };
    using SubmitFill = std::function<bool(size_t index, SubmitRequest& request)>;
    void SubmitMany(size_t count, const SubmitFill& fill);
    // Os objetos guardados de `world` (Fase 28), sem refazer caixa, matriz da normal e material de cada um. No
    // EndScene, só entram os que a câmera ou alguma sombra vê (pela grade do mundo). O mundo precisa continuar vivo e
    // sem mudar até o EndScene.
    void SubmitWorld(const RenderWorld& world);
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
        int alphaMode = -1, color = -1, pbr = -1, emissive = -1;
    };

    void LoadLitShaders(ShadowQuality cascadeFilter);
    // Quadro da cena (DrawScene): ordem de desenho, bloco Frame, lotes da cena e o desenho deles (opacos e
    // transparentes; o estado de shader, material e texturas lembrado vale dentro de uma chamada).
    void DrawScene();
    void SortDrawOrder(const Frustum& frustum);
    void UploadFrameData(const glm::mat4& view, const glm::mat4& projection);
    size_t CollectSceneBatches(std::vector<uint32_t>& indices, std::vector<DrawBatch>& batches);  // devolve os opacos
    // Lotes da cena a partir do lote `first` da passada gravada (InstanceBuffers::UploadPass).
    void DrawSceneBatches(std::span<const DrawBatch> batches, size_t first);
    void DrawWireframes();
    void BindTarget() const;
    void UploadJoints(const DrawItem& item);
    DrawItem& AddItem(const Mesh& mesh, const Material& material, const glm::mat4& model, ShadowCasting shadows);
    void PrepareItems(const Frustum& view);
    void AppendWorlds(const Frustum& view);  // os objetos dos RenderWorld que a câmera ou as sombras veem

    std::unique_ptr<Shader> m_litShader;
    std::unique_ptr<Shader> m_skinnedShader;  // skinned.vert + lit.frag
    std::unique_ptr<Shader> m_litPlainShader; // lit.frag com NO_MAPS: materiais sem nenhum mapa
    std::unique_ptr<Shader> m_unlitShader;
    LitUniforms m_litUniforms, m_skinnedUniforms, m_litPlainUniforms;
    ShadowQuality m_litShadowFilter = ShadowQuality::High;  // SHADOW_FILTER dos shaders acima
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
    std::vector<glm::mat4> m_objects;  // 2 por item (mundo e normal), na ordem de m_items: o buffer de objetos (só os
                                       // 2 * m_items.size() primeiros valem; não é esvaziado entre quadros)
    // Material do Submit anterior: o mesmo objeto, ou os mesmos valores, não consultam as tabelas.
    const Material* m_lastMaterial = nullptr;
    MaterialKey m_lastMaterialKey;
    size_t m_lastMaterialKeyHash = 0;  // do SubmitMany (o AddItem não o usa: só acelera a comparação)
    ShadowParams m_lastShadowParams;
    uint32_t m_lastMaterialId = 0, m_lastShadowId = 0;
    std::vector<uint32_t> m_sceneOrder, m_shadowOrder;
    std::vector<uint8_t> m_visible;  // por item: aparece na câmera (PrepareItems)
    // SubmitMany: pedidos montados em paralelo e as chaves de material de cada um (reaproveitados entre quadros).
    struct PendingSubmit {
        MaterialKey key;
        ShadowParams shadow;
        size_t keyHash = 0, shadowHash = 0;
        bool ok = false;
    };
    std::vector<PendingSubmit> m_pending;
    // SubmitWorld (Fase 28): os mundos do quadro e, no AppendWorlds, os números de material de cada um (do mundo ->
    // do quadro), o que a câmera e as passadas de sombra veem por entrada e as entradas escolhidas.
    std::vector<const RenderWorld*> m_worlds;
    std::vector<uint32_t> m_worldMaterials, m_worldShadows;
    std::vector<uint8_t> m_worldCamera;
    std::vector<uint32_t> m_worldMasks, m_worldHits;

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
