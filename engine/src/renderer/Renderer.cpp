#include "motor/renderer/Renderer.h"
#include "motor/renderer/RenderWorld.h"

#include "motor/core/EngineAssets.h"
#include "motor/core/JobSystem.h"
#include "motor/core/FrameStats.h"
#include "motor/core/Profile.h"
#include "motor/core/Log.h"
#include "motor/renderer/Framebuffer.h"
#include "motor/renderer/GpuMemory.h"
#include "motor/renderer/Pbr.h"
#include "renderer/DrawList.h"
#include "renderer/GL.h"
#include "renderer/GpuProfile.h"
#include "renderer/PostProcess.h"
#include "renderer/Shadows.h"
#include "renderer/Sky.h"

#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec4.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace motor {

namespace {
// Espelha o bloco "Frame" (std140) de frame.glsl. Só vec4/mat4 => layout idêntico ao C++.
struct FrameData {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec4 cameraPosition;
    glm::vec4 ambient;
    glm::vec4 fog;      // rgb = cor da neblina (linear), a = densidade
    glm::mat4 invViewProjection;
    glm::vec4 sky;      // x = brilho do fundo, y = brilho dos reflexos, z = rotação (rad), w = mip máximo do reflexo
    glm::vec4 sh[9];
    glm::vec4 viewport;
};

// Espelha o bloco "Lights" (std140) do lit.frag.
struct LightData {
    glm::vec4 positionRange;   // xyz = posição, w = alcance
    glm::vec4 directionType;   // xyz = direção (normalizada), w = tipo (LightType)
    glm::vec4 color;           // rgb = cor * intensidade
    glm::vec4 cone;            // x = cos(ângulo interno), y = cos(ângulo externo), z = camada do cubo de sombra, w = cascatas
};
struct LightsData {
    glm::ivec4 count;  // x = número de luzes
    LightData lights[kMaxLights];
};

constexpr GLuint kFrameBinding = 0;
constexpr GLuint kLightsBinding = 1;
constexpr GLuint kJointsBinding = 2;   // skinned.vert, shadow.vert
constexpr GLuint kShadowsBinding = 3;  // lit.frag

// Unidades de textura do lit.frag.
constexpr GLuint kUnitAlbedo = 0, kUnitNormal = 1, kUnitMetalRough = 2, kUnitOcclusion = 3, kUnitEmissive = 4;
constexpr GLuint kUnitCascades = 5, kUnitCubes = 6, kUnitPrefiltered = 7, kUnitBrdf = 8;
constexpr GLuint kUnitTerrainWeights = 9, kUnitTerrainLayers = 10;  // 10 a 13: as 4 camadas (lit.frag com TERRAIN)

LightData Pack(const Light& light, int cubeLayer, bool cascades, const glm::vec3& origin) {
    const float len = glm::length(light.direction);
    const glm::vec3 direction = len > 1e-6f ? light.direction / len : glm::vec3(0.0f, -1.0f, 0.0f);
    // Ângulos limitados para o cone não degenerar (externo > interno, abaixo de 90°).
    const float outer = glm::clamp(light.outerAngle, 1.0f, 89.0f);
    const float inner = glm::clamp(light.innerAngle, 0.0f, outer - 0.5f);
    return {
        glm::vec4(light.position - origin, glm::max(light.range, 0.01f)),  // relativa à câmera (Fase 28)
        glm::vec4(direction, static_cast<float>(light.type)),
        glm::vec4(light.color * glm::max(light.intensity, 0.0f), 1.0f),
        glm::vec4(std::cos(glm::radians(inner)), std::cos(glm::radians(outer)), static_cast<float>(cubeLayer),
                  cascades ? 1.0f : 0.0f),
    };
}

void APIENTRY OnGlDebug(GLenum, GLenum type, GLuint id, GLenum severity, GLsizei, const GLchar* message, const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH) {
        log::Error("OpenGL [{}]: {}", id, message);
    } else {
        log::Warn("OpenGL [{}]: {}", id, message);
    }
}

} // namespace

Renderer::Renderer() {
    // O contexto foi criado (e tornado atual) pela Window; aqui carregamos as funções que o renderer usa.
    if (!gl::Load([](const char* name) { return reinterpret_cast<void*>(glfwGetProcAddress(name)); })) {
        throw std::runtime_error("Falha ao carregar as funções do OpenGL");
    }
    log::Info("OpenGL {} | {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
              reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    GLint flags = 0;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(OnGlDebug, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
        log::Info("OpenGL debug output ativo");
    }
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);  // reflexos sem costura entre as faces do cubo
    MOTOR_PROFILE_GPU_CONTEXT();
    m_gpuTimers = std::make_unique<GpuTimers>();
    GpuTimers::SetCurrent(m_gpuTimers.get());

    const std::filesystem::path dir = EngineAssetsDir() / "shaders";
    LoadLitShaders(ShadowQuality::High);
    m_unlitShader = Shader::FromFiles(dir / "lit.vert", dir / "unlit.frag", "#define SINGLE_MODEL\n");
    m_unlitModel = m_unlitShader->Uniform("uModel");
    m_unlitColor = m_unlitShader->Uniform("uColor");
    m_waterShader = Shader::FromFiles(dir / "water.vert", dir / "water.frag");
    m_waterUniforms = {m_waterShader->Uniform("uModel"), m_waterShader->Uniform("uWave"),
                       m_waterShader->Uniform("uTime"), m_waterShader->Uniform("uShallow"),
                       m_waterShader->Uniform("uDeep"), m_waterShader->Uniform("uWater")};
    m_whiteTexture = Texture2D::CreateSolid({255, 255, 255, 255});
    m_instances = std::make_unique<InstanceBuffers>();
    m_post = std::make_unique<PostProcess>(dir);
    m_shadows = std::make_unique<ShadowRenderer>(dir);
    m_sky = std::make_unique<SkyRenderer>(dir);

    glCreateBuffers(1, &m_frameUbo);
    glNamedBufferStorage(m_frameUbo, sizeof(FrameData), nullptr, GL_DYNAMIC_STORAGE_BIT);
    glCreateBuffers(1, &m_lightsUbo);
    glNamedBufferStorage(m_lightsUbo, sizeof(LightsData), nullptr, GL_DYNAMIC_STORAGE_BIT);
    glCreateBuffers(1, &m_shadowsUbo);
    glNamedBufferStorage(m_shadowsUbo, sizeof(ShadowRenderer::Data), nullptr, GL_DYNAMIC_STORAGE_BIT);
    glCreateBuffers(1, &m_jointsUbo);
    glNamedBufferStorage(m_jointsUbo, sizeof(glm::mat4) * kMaxJoints, nullptr, GL_DYNAMIC_STORAGE_BIT);
    TrackGpuMemory(GpuObject::Buffer, m_frameUbo, GpuMemoryKind::Buffers, sizeof(FrameData));
    TrackGpuMemory(GpuObject::Buffer, m_lightsUbo, GpuMemoryKind::Buffers, sizeof(LightsData));
    TrackGpuMemory(GpuObject::Buffer, m_shadowsUbo, GpuMemoryKind::Buffers, sizeof(ShadowRenderer::Data));
    TrackGpuMemory(GpuObject::Buffer, m_jointsUbo, GpuMemoryKind::Buffers, sizeof(glm::mat4) * kMaxJoints);
}

Renderer::~Renderer() {
    for (unsigned int ubo : {m_jointsUbo, m_shadowsUbo, m_lightsUbo, m_frameUbo}) UntrackGpuMemory(GpuObject::Buffer, ubo);
    glDeleteBuffers(1, &m_jointsUbo);
    glDeleteBuffers(1, &m_shadowsUbo);
    glDeleteBuffers(1, &m_lightsUbo);
    glDeleteBuffers(1, &m_frameUbo);
}

// As três variantes do lit.frag com o filtro das cascatas da qualidade dada (SHADOW_FILTER). Recompiladas só quando a
// qualidade da direcional muda (trocar a cena ou as opções), não a cada quadro.
void Renderer::LoadLitShaders(ShadowQuality cascadeFilter) {
    const std::filesystem::path dir = EngineAssetsDir() / "shaders";
    const std::string filter = "#define SHADOW_FILTER " + std::to_string(static_cast<int>(cascadeFilter)) + "\n";
    m_litShader = Shader::FromFiles(dir / "lit.vert", dir / "lit.frag", filter);
    m_skinnedShader = Shader::FromFiles(dir / "skinned.vert", dir / "lit.frag", filter);  // animação esquelética (Fase 11)
    // Materiais sem nenhum mapa (o comum): sem as leituras de textura no shader. Um if pela uniform não basta (o
    // driver da Intel lê as texturas assim mesmo): ~4 ms por quadro na referência.
    m_litPlainShader = Shader::FromFiles(dir / "lit.vert", dir / "lit.frag", filter + "#define NO_MAPS\n");
    m_terrainShader = Shader::FromFiles(dir / "lit.vert", dir / "lit.frag", filter + "#define TERRAIN\n");
    // As uniforms das camadas só existem na variante TERRAIN: nas outras ficam -1 (sem o aviso de "não encontrado").
    auto findLitUniforms = [](Shader& shader, bool terrain) {
        return LitUniforms{shader.Uniform("uAlphaMode"), shader.Uniform("uColor"),
                           shader.Uniform("uPbr"), shader.Uniform("uEmissive"),
                           terrain ? shader.Uniform("uLayerColors") : -1, terrain ? shader.Uniform("uLayerTiling") : -1,
                           terrain ? shader.Uniform("uTerrainBlock") : -1};
    };
    m_litUniforms = findLitUniforms(*m_litShader, false);
    m_skinnedUniforms = findLitUniforms(*m_skinnedShader, false);
    m_litPlainUniforms = findLitUniforms(*m_litPlainShader, false);
    m_terrainUniforms = findLitUniforms(*m_terrainShader, true);
    m_litShadowFilter = cascadeFilter;
}

void Renderer::BeginFrame(int width, int height) {
    MOTOR_PROFILE_GPU_COLLECT();  // as zonas de GPU do quadro anterior
    m_gpuTimers->BeginFrame();    // e as dos contadores, de alguns quadros atrás
    m_stats = {};
    m_windowWidth = width;
    m_windowHeight = height;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClearColor(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::BeginScene(const Camera& camera, std::span<const Light> lights, const Environment& environment,
                          const Framebuffer* target) {
    MOTOR_ASSERT(!m_inScene, "BeginScene chamado duas vezes sem EndScene");
    m_inScene = true;
    m_target = target;
    m_camera = camera;
    m_environment = environment;
    // Tamanho na tela = diâmetro * m_lodScale / distância (altura vista a 1 m: 2 * tan(fov / 2)).
    m_lodScale = m_lodBias / std::max(2.0f * std::tan(glm::radians(camera.fovY) * 0.5f), 1e-3f);

    if (lights.size() > kMaxLights && !std::exchange(m_warnedTooManyLights, true)) {
        log::Warn("Renderer: {} luzes na cena; só as primeiras {} são usadas", lights.size(), kMaxLights);
    }
    m_lights.assign(lights.begin(), lights.begin() + static_cast<std::ptrdiff_t>(std::min(lights.size(), kMaxLights)));
    for (Light& light : m_lights) {
        light.castShadows = light.castShadows && m_shadowsEnabled;
        light.shadowQuality = std::min(light.shadowQuality, m_maxShadowQuality);
    }
    m_items.clear();
    m_joints.clear();
    m_wires.clear();
    m_waters.clear();
    m_worlds.clear();
    // m_objects não é esvaziado: vale até 2 * m_items.size(). Refazer 20 000 matrizes (o construtor do glm escreve a
    // identidade) custava ~0,7 ms por quadro com 10 000 objetos.
    m_materialIds.Clear();
    m_shadowIds.Clear();
    m_lastMaterial = nullptr;
}

DrawItem& Renderer::AddItem(const Mesh& mesh, const Material& material, const glm::mat4& model, ShadowCasting shadows) {
    // Os números do material saem aqui, com o material ainda no cache (quem chama acabou de lê-lo): no EndScene,
    // ler milhares de materiais espalhados na memória custava mais que as contas. O mesmo material seguido, ou com
    // os mesmos valores (cada objeto com o seu, todos iguais), não consulta as tabelas.
    if (&material != m_lastMaterial) {
        const bool first = m_lastMaterial == nullptr;
        m_lastMaterial = &material;
        const MaterialKey key{MaterialParamsOf(material), material.doubleSided};
        if (first || !(key == m_lastMaterialKey)) {
            m_lastMaterialId = m_materialIds.Id(key);
            m_lastMaterialKey = key;
        }
        const ShadowParams shadow = ShadowParamsOf(material);
        if (first || !(shadow == m_lastShadowParams)) {
            m_lastShadowId = m_shadowIds.Id(shadow);
            m_lastShadowParams = shadow;
        }
    }
    DrawItem& item = m_items.emplace_back();
    item.mesh = &mesh;
    item.material = &material;
    item.materialId = m_lastMaterialId;
    item.shadowId = m_lastShadowId;
    item.transparent = material.blend == BlendMode::Transparent;
    item.shadows = shadows;
    // A matriz da normal e a caixa no mundo saem no EndScene, em paralelo (PrepareItems): aqui só a de mundo.
    const size_t object = 2 * (m_items.size() - 1);
    if (m_objects.size() < object + 2) m_objects.resize(object + 2);
    m_objects[object] = model;
    return item;
}

void Renderer::Submit(const Mesh& mesh, const Material& material, const glm::mat4& model, ShadowCasting shadows) {
    MOTOR_ASSERT(m_inScene, "Submit fora de BeginScene/EndScene");
    AddItem(mesh, material, model, shadows);
}

void Renderer::SubmitMany(size_t count, const SubmitFill& fill) {
    MOTOR_ASSERT(m_inScene, "SubmitMany fora de BeginScene/EndScene");
    MOTOR_PROFILE_SCOPE("Renderer: SubmitMany");
    JobSystem& jobs = JobSystem::Get();
    const size_t base = m_items.size();
    m_items.resize(base + count);
    if (m_objects.size() < 2 * (base + count)) m_objects.resize(2 * (base + count));
    m_pending.resize(count);

    // 1) Em paralelo: cada pedido já no seu lugar (base + i), com as chaves do material e os hashes delas. Ler os
    //    objetos e materiais (espalhados na memória) é o que custa.
    jobs.ParallelFor(count, 256, [&](size_t begin, size_t end) {
        SubmitRequest request;
        for (size_t i = begin; i < end; ++i) {
            PendingSubmit& p = m_pending[i];
            request = SubmitRequest{};
            p.ok = fill(i, request) && request.mesh && request.material;
            if (!p.ok) continue;
            const Material& material = *request.material;
            p.key = {MaterialParamsOf(material), material.doubleSided};
            p.keyHash = MaterialKeyHash{}(p.key);
            p.shadow = ShadowParamsOf(material);
            p.shadowHash = ShadowParamsHash{}(p.shadow);
            DrawItem& item = m_items[base + i];
            item = DrawItem{};
            item.mesh = request.mesh;
            item.material = request.material;
            item.transparent = material.blend == BlendMode::Transparent;
            item.shadows = request.shadows;
            m_objects[2 * (base + i)] = request.model;  // a matriz da normal sai no PrepareItems
        }
    });

    // 2) Números de material na ordem dos pedidos (a tabela é uma só): serial, comparando primeiro os hashes. Mesmo
    //    atalho do AddItem: o mesmo material, ou os mesmos valores, não consultam as tabelas. Pedido recusado (raro):
    //    os seguintes descem uma posição.
    size_t slot = base;
    for (size_t i = 0; i < count; ++i) {
        const PendingSubmit& p = m_pending[i];
        if (!p.ok) continue;
        const Material* material = m_items[base + i].material;
        if (material != m_lastMaterial) {
            const bool first = m_lastMaterial == nullptr;
            m_lastMaterial = material;
            if (first || p.keyHash != m_lastMaterialKeyHash || !(p.key == m_lastMaterialKey)) {
                m_lastMaterialId = m_materialIds.Id(p.key, p.keyHash);
                m_lastMaterialKey = p.key;
                m_lastMaterialKeyHash = p.keyHash;
            }
            if (first || !(p.shadow == m_lastShadowParams)) {
                m_lastShadowId = m_shadowIds.Id(p.shadow, p.shadowHash);
                m_lastShadowParams = p.shadow;
            }
        }
        if (slot != base + i) {
            m_items[slot] = m_items[base + i];
            m_objects[2 * slot] = m_objects[2 * (base + i)];
        }
        m_items[slot].materialId = m_lastMaterialId;
        m_items[slot].shadowId = m_lastShadowId;
        ++slot;
    }
    m_items.resize(slot);
}

void Renderer::SubmitWorld(const RenderWorld& world) {
    MOTOR_ASSERT(m_inScene, "SubmitWorld fora de BeginScene/EndScene");
    if (world.Count() > 0) m_worlds.push_back(&world);
}

void Renderer::AppendWorlds(const Frustum& view) {
    MOTOR_PROFILE_STAT("Render: recortar os guardados");
    JobSystem& jobs = JobSystem::Get();
    for (const RenderWorld* world : m_worlds) {
        const size_t count = world->m_entries.size();
        // Os números de material do mundo viram os do quadro uma vez por material diferente, não por objeto.
        m_worldMaterials.resize(world->m_materials.Size());
        for (uint32_t i = 0; i < m_worldMaterials.size(); ++i) {
            m_worldMaterials[i] = m_materialIds.Id(world->m_materials[i]);
        }
        m_worldShadows.resize(world->m_shadows.Size());
        for (uint32_t i = 0; i < m_worldShadows.size(); ++i) m_worldShadows[i] = m_shadowIds.Id(world->m_shadows[i]);

        // Recorte pela grade (Fase 28): a câmera e cada passada de sombra perguntam à grade quem elas veem. Só entram
        // no quadro os objetos que alguém vê; os outros nem são lidos.
        m_worldCamera.assign(count, 0);
        m_worldMasks.assign(count, 0);
        m_worldHits.clear();
        world->QueryEntries(view, m_worldHits);
        for (const uint32_t e : m_worldHits) m_worldCamera[e] = 1;
        for (size_t p = 0; p < m_shadows->PassCount(); ++p) {
            const Frustum* frustum = m_shadows->PassFrustum(p);
            if (!frustum) continue;
            m_worldHits.clear();
            world->QueryEntries(*frustum, m_worldHits);
            for (const uint32_t e : m_worldHits) m_worldMasks[e] |= 1u << p;
        }
        m_worldHits.clear();
        for (uint32_t e = 0; e < count; ++e) {
            if (m_worldCamera[e] || m_worldMasks[e]) m_worldHits.push_back(e);
        }
        m_stats.culled += static_cast<uint32_t>(count - m_worldHits.size());  // os de fora de tudo

        const size_t base = m_items.size(), selected = m_worldHits.size();
        m_items.resize(base + selected);
        if (m_objects.size() < 2 * (base + selected)) m_objects.resize(2 * (base + selected));
        jobs.ParallelFor(selected, 1024, [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                const uint32_t e = m_worldHits[i];
                const RenderWorld::Entry& entry = world->m_entries[e];
                DrawItem& item = m_items[base + i];
                item = DrawItem{};
                item.mesh = entry.mesh.get();
                item.material = entry.material.get();
                item.bounds = entry.bounds;
                item.materialId = m_worldMaterials[entry.materialId];
                item.shadowId = m_worldShadows[entry.shadowId];
                item.transparent = entry.transparent;
                item.shadows = entry.shadows;
                item.retained = true;
                item.cameraVisible = m_worldCamera[e] != 0;
                item.shadowMask = m_worldMasks[e];
                m_objects[2 * (base + i)] = world->m_objects[2 * e];
                m_objects[2 * (base + i) + 1] = world->m_objects[2 * e + 1];
            }
        });
    }
}

void Renderer::SubmitWireframe(const Mesh& mesh, const glm::mat4& model, const glm::vec4& color) {
    MOTOR_ASSERT(m_inScene, "SubmitWireframe fora de BeginScene/EndScene");
    m_wires.push_back({&mesh, model, color});
}

void Renderer::SubmitSkinned(const Mesh& mesh, const Material& material, const glm::mat4& model,
                             std::span<const glm::mat4> joints, ShadowCasting shadows) {
    MOTOR_ASSERT(m_inScene, "SubmitSkinned fora de BeginScene/EndScene");
    if (joints.size() > kMaxJoints && !std::exchange(m_warnedTooManyJoints, true)) {
        log::Warn("Renderer: malha com {} juntas; só as primeiras {} se movem", joints.size(), kMaxJoints);
    }
    const size_t count = std::min(joints.size(), kMaxJoints);
    DrawItem& item = AddItem(mesh, material, model, shadows);
    item.skinned = true;
    item.jointOffset = static_cast<uint32_t>(m_joints.size());
    item.jointCount = static_cast<uint32_t>(count);
    m_joints.insert(m_joints.end(), joints.begin(), joints.begin() + static_cast<std::ptrdiff_t>(count));
}

void Renderer::PrepareItems(const Frustum& view) {
    MOTOR_PROFILE_STAT("Render: preparar");
    // Por item: matriz da normal, caixa no mundo, chaves de ordem e recorte da câmera. Cada item só escreve no que é
    // dele, então os itens se dividem entre as threads sem trava (Fase 26).
    const size_t count = m_items.size();
    m_sceneKeys.resize(count);
    m_shadowKeys.resize(count);
    m_visible.resize(count);
    JobSystem::Get().ParallelFor(count, 256, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            DrawItem& item = m_items[i];
            const glm::mat4& model = m_objects[2 * i];
            const glm::vec3 min = item.mesh->BoundsMin(), max = item.mesh->BoundsMax();
            if (item.skinned) {
                // A pose animada sai da caixa de repouso: folga de 50% (e meio metro) para o recorte não sumir com ela.
                m_objects[2 * i + 1] = glm::mat4(NormalMatrix(model));
                const glm::vec3 center = (min + max) * 0.5f;
                const glm::vec3 half = (max - min) * 0.75f + glm::vec3(0.5f);
                item.bounds = TransformAabb({center - half, center + half}, model);
            } else {
                // Os do RenderWorld (Fase 28) já trazem a caixa e a matriz da normal.
                if (!item.retained) {
                    m_objects[2 * i + 1] = glm::mat4(NormalMatrix(model));
                    item.bounds = TransformAabb({min, max}, model);
                }
                if (!item.mesh->Lods().empty()) {
                    // Nível de detalhe pelo tamanho na tela (Fase 24): diâmetro da caixa no mundo sobre a altura vista
                    // na distância dela. A caixa continua a da malha base (os níveis têm os mesmos limites).
                    const glm::vec3 center = (item.bounds.min + item.bounds.max) * 0.5f;
                    const float diameter = glm::length(item.bounds.max - item.bounds.min);
                    const float distance = std::max(glm::length(center - m_camera.position), 1e-3f);
                    item.mesh = &item.mesh->ForScreenSize(diameter * m_lodScale / distance);
                }
            }
            // Ordem de desenho: (shader, material, malha) para a cena e (shader, recorte/alfa, malha) para as sombras.
            // Shader: 0 = lit, 1 = com pele, 2 = lit sem mapas, 3 = terreno com camadas.
            const MaterialParams& params = m_materialIds[item.materialId].params;
            const bool plain = params.alphaMode.z == 0.0f && params.pbr.w == 0.0f;
            const uint32_t shader = item.skinned ? 1u : params.terrain ? 3u : plain ? 2u : 0u;
            m_sceneKeys[i] = {shader, item.materialId, item.mesh};
            m_shadowKeys[i] = {shader, item.shadowId, item.mesh};
            const bool inView = item.retained ? item.cameraVisible : view.Intersects(item.bounds);
            m_visible[i] = item.shadows != ShadowCasting::ShadowsOnly && inView;
            // Para a GPU, relativa à câmera (Fase 28): a caixa e o LOD acima já saíram no mundo.
            m_objects[2 * i] = ModelRelativeToOrigin(model, m_camera.position);
        }
    });
}

void Renderer::UploadJoints(const DrawItem& item) {
    glNamedBufferSubData(m_jointsUbo, 0, static_cast<GLsizeiptr>(item.jointCount * sizeof(glm::mat4)),
                         m_joints.data() + item.jointOffset);
    glBindBufferBase(GL_UNIFORM_BUFFER, kJointsBinding, m_jointsUbo);
}

void Renderer::BindTarget() const {
    if (m_target) {
        m_target->Bind();
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, m_windowWidth, m_windowHeight);
    }
}

void Renderer::EndScene() {
    MOTOR_ASSERT(m_inScene, "EndScene sem BeginScene");
    MOTOR_PROFILE_GPU("Renderer");
    m_inScene = false;

    // Outras camadas (ex.: ImGui) mexem no estado do GL; reafirmamos o que a cena precisa.
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);

    const int width = m_target ? m_target->Width() : std::max(m_windowWidth, 1);
    const int height = m_target ? m_target->Height() : std::max(m_windowHeight, 1);
    m_post->Resize(width, height, m_samples);
    {
        MOTOR_PROFILE_GPU("Céu");
        m_sky->Update(m_environment, *m_post);
    }

    {
        MOTOR_PROFILE_STAT("Render: cena e sombras");
        DrawScene();
    }
    {
        MOTOR_PROFILE_GPU("Pós-processamento");
        m_post->Resolve(m_keepSceneDepth || !m_wires.empty());  // a profundidade só para quem a lê
        m_post->Apply(m_environment, [this] { BindTarget(); });
    }
    DrawWireframes();

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    if (m_target) m_target->Resolve();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_windowWidth, m_windowHeight);
    m_target = nullptr;
    m_instances->Fence();  // objetos, índices e comandos desta cena: o anel só os reescreve depois da GPU
}

void Renderer::DrawScene() {
    const JointUploader uploadJoints = [this](const DrawItem& item) { UploadJoints(item); };

    // 0) Ordem de desenho (SortDrawOrder). O recorte é no mundo; a GPU recebe tudo relativo à câmera (Fase 28):
    //    a vista sem translação, os objetos, as luzes e as sombras menos a posição dela.
    //    Antes, as passadas de sombra e os objetos guardados que a câmera ou elas veem (pela grade).
    const glm::vec3 origin = m_camera.position;
    const glm::mat4 projection = m_camera.Projection();
    const Frustum viewFrustum = Frustum::FromMatrix(projection * m_camera.View());
    m_shadows->Plan(m_camera, m_lights, m_environment);
    AppendWorlds(viewFrustum);
    SortDrawOrder(viewFrustum);
    const glm::mat4 view = m_camera.ViewRotation();

    // 1) Mapas de sombra. Antes, os objetos de todos os itens (as passadas desenham por índice neles).
    m_instances->UploadObjects(std::span(m_objects.data(), 2 * m_items.size()));
    const ShadowRenderer::Result shadows = [&] {
        MOTOR_PROFILE_GPU("Sombras");
        return m_shadows->Render(origin, m_items, *m_instances, {m_shadowOrder, m_shadowKeys, &m_shadowIds},
                                 uploadJoints, m_stats);
    }();

    // 2) Blocos de dados do quadro.
    UploadFrameData(view, projection);
    LightsData lightsData{};
    lightsData.count = glm::ivec4(static_cast<int>(m_lights.size()), 0, 0, 0);
    for (size_t i = 0; i < m_lights.size(); ++i) {
        const bool cascades = shadows.cascadeLight == static_cast<int>(i);
        lightsData.lights[i] = Pack(m_lights[i], shadows.cubeLayer[i], cascades, origin);
    }
    // Só o cabeçalho e as luzes usadas: o resto do bloco o shader não lê.
    glNamedBufferSubData(m_lightsUbo, 0, static_cast<GLsizeiptr>(sizeof(glm::ivec4) + m_lights.size() * sizeof(LightData)), &lightsData);
    glBindBufferBase(GL_UNIFORM_BUFFER, kLightsBinding, m_lightsUbo);
    if (shadows.cascadeLight >= 0 && shadows.cascadeQuality != m_litShadowFilter) LoadLitShaders(shadows.cascadeQuality);
    glNamedBufferSubData(m_shadowsUbo, 0, sizeof(ShadowRenderer::Data), &shadows.data);
    glBindBufferBase(GL_UNIFORM_BUFFER, kShadowsBinding, m_shadowsUbo);

    // 3) Cena em HDR.
    MOTOR_PROFILE_GPU("Cena HDR");  // opacos, céu ao fundo e transparentes
    m_post->BeginScene();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_MULTISAMPLE);
    glPolygonMode(GL_FRONT_AND_BACK, m_wireframe ? GL_LINE : GL_FILL);

    glBindTextureUnit(kUnitCascades, m_shadows->CascadeTexture());
    glBindTextureUnit(kUnitCubes, m_shadows->CubeTexture());
    glBindTextureUnit(kUnitPrefiltered, m_sky->PrefilteredCube());
    glBindTextureUnit(kUnitBrdf, m_sky->BrdfLut());

    std::vector<uint32_t> indices;
    std::vector<DrawBatch> batches;
    const size_t opaqueBatches = CollectSceneBatches(indices, batches);
    m_instances->UploadPass(indices, batches);
    DrawSceneBatches(std::span(batches).first(opaqueBatches), 0);

    // 4) Céu ao fundo, onde não há nada. Depois a água, que lê a cópia de tudo isso (e grava profundidade: os
    //    transparentes atrás dela ficam escondidos).
    m_sky->DrawBackground(*m_post);
    if (!m_waters.empty()) DrawWater(viewFrustum);

    // 5) Transparentes: misturados com o que já está na imagem, sem gravar profundidade (o que está atrás de um
    //    vidro continua aparecendo pelo vidro seguinte).
    if (batches.size() > opaqueBatches) {
        MOTOR_PROFILE_GPU("Transparentes");
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        DrawSceneBatches(std::span(batches).subspan(opaqueBatches), opaqueBatches);  // o céu trocou o shader e as texturas
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        m_stats.transparent += static_cast<uint32_t>(batches.size() - opaqueBatches);
    }
}

void Renderer::SortDrawOrder(const Frustum& frustum) {
    // A lista é ordenada por (shader, material, malha) com os números de material dados no Submit (valores iguais,
    // o mesmo número), uma vez para a cena e outra para as sombras (que só olham recorte e alfa). Itens que podem
    // ir no mesmo draw ficam seguidos. As chaves (e o resto por item) saem em paralelo.
    PrepareItems(frustum);
    // Cena parada: as mesmas chaves do quadro anterior dão a mesma ordem.
    auto sortIfChanged = [](const std::vector<DrawKey>& keys, std::vector<DrawKey>& lastKeys,
                            std::vector<uint32_t>& order) {
        if (keys == lastKeys && order.size() == keys.size()) return;
        SortByKey(keys, order);
        lastKeys = keys;
    };
    sortIfChanged(m_sceneKeys, m_lastSceneKeys, m_sceneOrder);
    sortIfChanged(m_shadowKeys, m_lastShadowKeys, m_shadowOrder);
}

void Renderer::UploadFrameData(const glm::mat4& view, const glm::mat4& projection) {
    const Environment& env = m_environment;
    const float skyIntensity = std::max(env.skyIntensity, 0.0f);
    const float skyLight = skyIntensity * std::max(env.skyLight, 0.0f);
    FrameData frame{};
    frame.view = view;
    frame.projection = projection;
    frame.cameraPosition = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);  // a câmera é a origem do que a GPU recebe (Fase 28)
    frame.ambient = glm::vec4(env.ambient, 1.0f);
    frame.fog = glm::vec4(m_sky->HorizonColor() * skyIntensity, std::max(env.fogDensity, 0.0f));
    frame.invViewProjection = glm::inverse(projection * view);
    frame.sky = glm::vec4(skyIntensity, skyLight, glm::radians(env.skyRotation), m_sky->MaxReflectionMip());
    for (size_t i = 0; i < 9; ++i) frame.sh[i] = glm::vec4(m_sky->Irradiance().c[i] * skyLight, 0.0f);
    frame.viewport = glm::vec4(static_cast<float>(m_post->Width()), static_cast<float>(m_post->Height()),
                               1.0f / static_cast<float>(m_post->Width()), 1.0f / static_cast<float>(m_post->Height()));
    glNamedBufferSubData(m_frameUbo, 0, sizeof(FrameData), &frame);
    glBindBufferBase(GL_UNIFORM_BUFFER, kFrameBinding, m_frameUbo);
}

size_t Renderer::CollectSceneBatches(std::vector<uint32_t>& indices, std::vector<DrawBatch>& batches) {
    // Lotes: na ordem da cena, itens seguidos com a mesma chave (shader, material, malha) viram um draw
    // instanciado. Com pele, cada item é um lote (as juntas são dele). Transparentes: um por vez, de trás para
    // frente, depois do céu. O recorte da câmera já saiu no PrepareItems (m_visible).
    std::vector<std::pair<float, uint32_t>> transparent;
    indices.reserve(m_items.size());
    auto add = [&](uint32_t index, bool join) {
        const DrawItem& item = m_items[index];
        if (join && !batches.empty() && !item.skinned && m_sceneKeys[index] == m_sceneKeys[batches.back().firstIndex]) {
            ++batches.back().count;
            indices.push_back(index);
            return;
        }
        batches.push_back({&item, static_cast<uint32_t>(indices.size()), 1, index});
        indices.push_back(index);
    };
    for (const uint32_t i : m_sceneOrder) {
        const DrawItem& item = m_items[i];
        if (item.shadows == ShadowCasting::ShadowsOnly) continue;  // só nos mapas de sombra
        if (!m_visible[i]) {
            ++m_stats.culled;
            continue;
        }
        if (item.transparent) {
            const glm::vec3 center = (item.bounds.min + item.bounds.max) * 0.5f;
            const glm::vec3 toCamera = center - m_camera.position;
            transparent.emplace_back(glm::dot(toCamera, toCamera), i);
            continue;
        }
        add(i, true);
    }
    const size_t opaqueBatches = batches.size();
    // Transparentes ordenados pelo centro: do mais longe ao mais perto.
    std::stable_sort(transparent.begin(), transparent.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
    for (const auto& [distance, index] : transparent) add(index, false);
    return opaqueBatches;
}

void Renderer::DrawSceneBatches(std::span<const DrawBatch> batches, size_t first) {
    const Shader* bound = nullptr;
    // Material e texturas do lote anterior: lotes seguidos com o mesmo material não reenviam nada disso. Os
    // uniforms são de cada programa, por isso a lembrança vale para um shader só.
    const Shader* paramsShader = nullptr;
    const MaterialParams* lastParams = nullptr;
    GLuint lastTexture[kUnitTerrainLayers + 4] = {};
    auto bindTexture = [&](GLuint unit, GLuint texture) {
        if (lastTexture[unit] == texture) return;
        lastTexture[unit] = texture;
        glBindTextureUnit(unit, texture);
    };
    const Texture2D& white = *m_whiteTexture;
    // Lotes seguidos com a mesma chave de shader e material, sem pele e no mesmo VAO (todas as malhas sem pele dividem
    // um) saem num draw indireto só (Fase 28), mesmo com malhas diferentes. Transparentes também: os comandos de um
    // draw são desenhados na ordem.
    auto vao = [](const DrawBatch& b) { return b.first->mesh->Gpu().vao; };
    for (size_t i = 0; i < batches.size();) {
        const DrawBatch& batch = batches[i];
        const DrawItem& item = *batch.first;
        const DrawKey& key = m_sceneKeys[batch.firstIndex];
        size_t end = i + 1;
        if (!item.skinned) {
            while (end < batches.size() && !batches[end].first->skinned && vao(batches[end]) == vao(batch)) {
                const DrawKey& next = m_sceneKeys[batches[end].firstIndex];
                if (next.shader != key.shader || next.material != key.material) break;
                ++end;
            }
        }
        const uint32_t variant = key.shader;
        Shader& shader = variant == 1   ? *m_skinnedShader
                         : variant == 2 ? *m_litPlainShader
                         : variant == 3 ? *m_terrainShader
                                        : *m_litShader;
        const LitUniforms& u = variant == 1   ? m_skinnedUniforms
                               : variant == 2 ? m_litPlainUniforms
                               : variant == 3 ? m_terrainUniforms
                                              : m_litUniforms;
        if (bound != &shader) {
            shader.Bind();
            bound = &shader;
        }
        if (item.skinned) UploadJoints(item);
        const bool doubleSided = item.material->doubleSided;
        // Duas faces: sem descartar as de trás (o lit.frag vira a normal delas).
        if (doubleSided) glDisable(GL_CULL_FACE);
        const MaterialParams& params = m_materialIds[m_sceneKeys[batch.firstIndex].material].params;
        if (paramsShader != &shader || !lastParams || !(params == *lastParams)) {
            shader.SetVec4(u.alphaMode, params.alphaMode);
            shader.SetVec4(u.color, params.color);
            shader.SetVec4(u.pbr, params.pbr);
            shader.SetVec4(u.emissive, params.emissive);
            bindTexture(kUnitAlbedo, (params.albedo ? *params.albedo : white).NativeHandle());
            bindTexture(kUnitNormal, (params.normalMap ? *params.normalMap : white).LinearHandle());
            bindTexture(kUnitMetalRough, (params.metallicRoughnessMap ? *params.metallicRoughnessMap : white).LinearHandle());
            bindTexture(kUnitOcclusion, (params.occlusionMap ? *params.occlusionMap : white).LinearHandle());
            bindTexture(kUnitEmissive, (params.emissiveMap ? *params.emissiveMap : white).NativeHandle());
            if (const TerrainLayers* layers = params.terrain) {
                const glm::mat4 colors(layers->color[0], layers->color[1], layers->color[2], layers->color[3]);
                shader.SetMat4(u.layerColors, colors);
                shader.SetVec4(u.layerTiling, layers->tiling);
                shader.SetVec4(u.terrainBlock, layers->block);
                bindTexture(kUnitTerrainWeights, (layers->weights ? *layers->weights : white).NativeHandle());
                for (GLuint i = 0; i < 4; ++i) {
                    bindTexture(kUnitTerrainLayers + i, (layers->albedo[i] ? *layers->albedo[i] : white).NativeHandle());
                }
            }
            lastParams = &params;
            paramsShader = &shader;
        }
        m_instances->Draw(first + i, end - i);
        if (doubleSided) glEnable(GL_CULL_FACE);
        ++m_stats.drawCalls;
        for (size_t b = i; b < end; ++b) m_stats.triangles += batches[b].first->mesh->IndexCount() / 3 * batches[b].count;
        i = end;
    }
}

void Renderer::DrawWireframes() {
    if (m_wires.empty()) return;
    // O alvo já está vinculado (Apply). A profundidade da cena vem da textura (o alvo pode ter outro MSAA).
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    m_unlitShader->Bind();
    glBindTextureUnit(0, m_post->SceneDepth());
    for (const WireItem& wire : m_wires) {
        m_unlitShader->SetMat4(m_unlitModel, ModelRelativeToOrigin(wire.model, m_camera.position));
        m_unlitShader->SetVec4(m_unlitColor, wire.color);
        wire.mesh->Draw();
        ++m_stats.drawCalls;
    }
}

unsigned int Renderer::SceneDepthTexture() const { return m_post->SceneDepth(); }
unsigned int Renderer::SceneColorTexture() const { return m_post->SceneColor(); }
unsigned int Renderer::SkyCubemap() const { return m_sky->EnvironmentCube(); }
unsigned int Renderer::ReflectionCubemap() const { return m_sky->PrefilteredCube(); }

} // namespace motor
