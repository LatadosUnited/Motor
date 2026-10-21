#include "motor/renderer/Renderer.h"

#include "motor/core/EngineAssets.h"
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
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
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

LightData Pack(const Light& light, int cubeLayer, bool cascades) {
    const float len = glm::length(light.direction);
    const glm::vec3 direction = len > 1e-6f ? light.direction / len : glm::vec3(0.0f, -1.0f, 0.0f);
    // Ângulos limitados para o cone não degenerar (externo > interno, abaixo de 90°).
    const float outer = glm::clamp(light.outerAngle, 1.0f, 89.0f);
    const float inner = glm::clamp(light.innerAngle, 0.0f, outer - 0.5f);
    return {
        glm::vec4(light.position, glm::max(light.range, 0.01f)),
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
    m_litShader = Shader::FromFiles(dir / "lit.vert", dir / "lit.frag");
    m_skinnedShader = Shader::FromFiles(dir / "skinned.vert", dir / "lit.frag");  // animação esquelética (Fase 11)
    m_unlitShader = Shader::FromFiles(dir / "lit.vert", dir / "unlit.frag", "#define SINGLE_MODEL\n");
    auto findLitUniforms = [](Shader& shader) {
        return LitUniforms{shader.Uniform("uAlphaMode"), shader.Uniform("uDrawBase"), shader.Uniform("uColor"),
                           shader.Uniform("uPbr"), shader.Uniform("uEmissive")};
    };
    m_litUniforms = findLitUniforms(*m_litShader);
    m_skinnedUniforms = findLitUniforms(*m_skinnedShader);
    m_unlitModel = m_unlitShader->Uniform("uModel");
    m_unlitColor = m_unlitShader->Uniform("uColor");
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
}

void Renderer::Submit(const Mesh& mesh, const Material& material, const glm::mat4& model, ShadowCasting shadows) {
    MOTOR_ASSERT(m_inScene, "Submit fora de BeginScene/EndScene");
    DrawItem& item = m_items.emplace_back();
    item.mesh = &mesh;
    item.material = &material;
    item.model = model;
    item.shadows = shadows;
    item.bounds = TransformAabb({mesh.BoundsMin(), mesh.BoundsMax()}, model);
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
    DrawItem& item = m_items.emplace_back();
    item.mesh = &mesh;
    item.material = &material;
    item.model = model;
    item.skinned = true;
    item.shadows = shadows;
    item.jointOffset = static_cast<uint32_t>(m_joints.size());
    item.jointCount = static_cast<uint32_t>(count);
    m_joints.insert(m_joints.end(), joints.begin(), joints.begin() + static_cast<std::ptrdiff_t>(count));
    // A pose animada sai da caixa de repouso: folga de 50% (e meio metro) para o recorte não sumir com ela.
    const glm::vec3 center = (mesh.BoundsMin() + mesh.BoundsMax()) * 0.5f;
    const glm::vec3 half = (mesh.BoundsMax() - mesh.BoundsMin()) * 0.75f + glm::vec3(0.5f);
    item.bounds = TransformAabb({center - half, center + half}, model);
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

    DrawScene();
    {
        MOTOR_PROFILE_GPU("Pós-processamento");
        m_post->Resolve();
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
}

void Renderer::DrawScene() {
    const JointUploader uploadJoints = [this](const DrawItem& item) { UploadJoints(item); };

    // 1) Mapas de sombra. Antes, os objetos de todos os itens (as passadas desenham por índice neles).
    m_instances->UploadObjects(m_items);
    const ShadowRenderer::Result shadows = [&] {
        MOTOR_PROFILE_GPU("Sombras");
        return m_shadows->Render(m_camera, m_lights, m_environment, m_items, *m_instances, uploadJoints, m_stats);
    }();

    // 2) Blocos de dados do quadro.
    const Environment& env = m_environment;
    const float skyIntensity = std::max(env.skyIntensity, 0.0f);
    const float skyLight = skyIntensity * std::max(env.skyLight, 0.0f);
    const glm::mat4 view = m_camera.View();
    const glm::mat4 projection = m_camera.Projection();
    FrameData frame{};
    frame.view = view;
    frame.projection = projection;
    frame.cameraPosition = glm::vec4(m_camera.position, 1.0f);
    frame.ambient = glm::vec4(env.ambient, 1.0f);
    frame.fog = glm::vec4(m_sky->HorizonColor() * skyIntensity, std::max(env.fogDensity, 0.0f));
    frame.invViewProjection = glm::inverse(projection * view);
    frame.sky = glm::vec4(skyIntensity, skyLight, glm::radians(env.skyRotation), m_sky->MaxReflectionMip());
    for (size_t i = 0; i < 9; ++i) frame.sh[i] = glm::vec4(m_sky->Irradiance().c[i] * skyLight, 0.0f);
    frame.viewport = glm::vec4(static_cast<float>(m_post->Width()), static_cast<float>(m_post->Height()),
                               1.0f / static_cast<float>(m_post->Width()), 1.0f / static_cast<float>(m_post->Height()));
    glNamedBufferSubData(m_frameUbo, 0, sizeof(FrameData), &frame);
    glBindBufferBase(GL_UNIFORM_BUFFER, kFrameBinding, m_frameUbo);

    LightsData lightsData{};
    lightsData.count = glm::ivec4(static_cast<int>(m_lights.size()), 0, 0, 0);
    for (size_t i = 0; i < m_lights.size(); ++i) {
        lightsData.lights[i] = Pack(m_lights[i], shadows.cubeLayer[i], shadows.cascadeLight == static_cast<int>(i));
    }
    // Só o cabeçalho e as luzes usadas: o resto do bloco o shader não lê.
    glNamedBufferSubData(m_lightsUbo, 0, static_cast<GLsizeiptr>(sizeof(glm::ivec4) + m_lights.size() * sizeof(LightData)), &lightsData);
    glBindBufferBase(GL_UNIFORM_BUFFER, kLightsBinding, m_lightsUbo);
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

    // Lotes: itens seguidos com a mesma malha e o mesmo material (o mesmo objeto ou valores iguais) viram um draw
    // instanciado. Com pele, cada item é um lote (as juntas são dele). Transparentes: um por vez, de trás para
    // frente, depois do céu.
    const Frustum frustum = Frustum::FromMatrix(projection * view);
    std::vector<uint32_t> indices;
    std::vector<DrawBatch> batches;
    std::vector<MaterialParams> batchParams;  // o material de cada lote, já convertido
    std::vector<std::pair<float, uint32_t>> transparent;
    indices.reserve(m_items.size());
    auto add = [&](uint32_t index, bool join) {
        const DrawItem& item = m_items[index];
        if (join && !batches.empty()) {
            const DrawItem& first = *batches.back().first;
            const bool same = !item.skinned && !first.skinned && item.mesh == first.mesh &&
                              (item.material == first.material ||
                               (item.material->doubleSided == first.material->doubleSided &&
                                MaterialParamsOf(*item.material) == batchParams.back()));
            if (same) {
                ++batches.back().count;
                indices.push_back(index);
                return;
            }
        }
        batches.push_back({&item, static_cast<uint32_t>(indices.size()), 1});
        batchParams.push_back(MaterialParamsOf(*item.material));
        indices.push_back(index);
    };
    for (uint32_t i = 0; i < m_items.size(); ++i) {
        const DrawItem& item = m_items[i];
        if (item.shadows == ShadowCasting::ShadowsOnly) continue;  // só nos mapas de sombra
        if (!frustum.Intersects(item.bounds)) {
            ++m_stats.culled;
            continue;
        }
        if (item.material->blend == BlendMode::Transparent) {
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
    m_instances->UploadIndices(indices);

    const Shader* bound = nullptr;
    // Material e texturas do lote anterior: lotes seguidos com o mesmo material não reenviam nada disso. Os
    // uniforms são de cada programa, por isso a lembrança vale para um shader só.
    const Shader* paramsShader = nullptr;
    const MaterialParams* lastParams = nullptr;
    GLuint lastTexture[5] = {0, 0, 0, 0, 0};
    auto bindTexture = [&](GLuint unit, GLuint texture) {
        if (lastTexture[unit] == texture) return;
        lastTexture[unit] = texture;
        glBindTextureUnit(unit, texture);
    };
    const Texture2D& white = *m_whiteTexture;
    auto draw = [&](size_t b) {
        const DrawBatch& batch = batches[b];
        const DrawItem& item = *batch.first;
        Shader& shader = item.skinned ? *m_skinnedShader : *m_litShader;
        const LitUniforms& u = item.skinned ? m_skinnedUniforms : m_litUniforms;
        if (bound != &shader) {
            shader.Bind();
            bound = &shader;
        }
        if (item.skinned) UploadJoints(item);
        const bool doubleSided = item.material->doubleSided;
        // Duas faces: sem descartar as de trás (o lit.frag vira a normal delas).
        if (doubleSided) glDisable(GL_CULL_FACE);
        const MaterialParams& params = batchParams[b];
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
            lastParams = &params;
            paramsShader = &shader;
        }
        shader.SetInt(u.drawBase, static_cast<int>(batch.start));
        item.mesh->DrawInstanced(batch.count);
        if (doubleSided) glEnable(GL_CULL_FACE);
        ++m_stats.drawCalls;
        m_stats.triangles += item.mesh->IndexCount() / 3 * batch.count;
    };
    for (size_t b = 0; b < opaqueBatches; ++b) draw(b);

    // 4) Céu ao fundo, onde não há nada.
    m_sky->DrawBackground(*m_post);

    // 5) Transparentes: misturados com o que já está na imagem, sem gravar profundidade (o que está atrás de um
    //    vidro continua aparecendo pelo vidro seguinte).
    if (!transparent.empty()) {
        MOTOR_PROFILE_GPU("Transparentes");
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        bound = nullptr;  // o céu trocou o shader e as texturas
        paramsShader = nullptr;
        std::fill(std::begin(lastTexture), std::end(lastTexture), 0u);
        for (size_t b = opaqueBatches; b < batches.size(); ++b) draw(b);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        m_stats.transparent += static_cast<uint32_t>(transparent.size());
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
        m_unlitShader->SetMat4(m_unlitModel, wire.model);
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
