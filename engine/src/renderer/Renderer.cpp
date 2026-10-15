#include "motor/renderer/Renderer.h"

#include "motor/core/Log.h"
#include "motor/renderer/Framebuffer.h"
#include "renderer/GL.h"

#include <glm/exponential.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace motor {

namespace {
// Espelha o bloco "Frame" (std140) dos shaders. Só vec4/mat4 => layout idêntico ao C++.
struct FrameData {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec4 cameraPosition;
    glm::vec4 lightDirection;
    glm::vec4 lightColor;  // rgb * intensidade
    glm::vec4 ambient;
    glm::vec4 fog;         // rgb = cor do céu (linear), a = densidade
};
constexpr GLuint kFrameBinding = 0;

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
    GLint flags = 0;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(OnGlDebug, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
        log::Info("OpenGL debug output ativo");
    }

    const std::string dir = MOTOR_ENGINE_ASSETS "/shaders/";
    m_litShader = Shader::FromFiles(dir + "lit.vert", dir + "lit.frag");
    m_unlitShader = Shader::FromFiles(dir + "lit.vert", dir + "unlit.frag");
    m_whiteTexture = Texture2D::CreateSolid({255, 255, 255, 255});

    glCreateBuffers(1, &m_frameUbo);
    glNamedBufferStorage(m_frameUbo, sizeof(FrameData), nullptr, GL_DYNAMIC_STORAGE_BIT);
}

Renderer::~Renderer() {
    glDeleteBuffers(1, &m_frameUbo);
}

void Renderer::BeginFrame(int width, int height) {
    m_stats = {};
    m_windowWidth = width;
    m_windowHeight = height;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClearColor(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::BeginScene(const Camera& camera, const DirectionalLight& light,
                          const Environment& environment, const Framebuffer* target) {
    MOTOR_ASSERT(!m_inScene, "BeginScene chamado duas vezes sem EndScene");
    m_inScene = true;
    m_target = target;

    if (target) {
        target->Bind();
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, m_windowWidth, m_windowHeight);
    }

    // Outras camadas (ex.: ImGui) mexem no estado do GL; reafirmamos o que a cena precisa.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_MULTISAMPLE);
    glPolygonMode(GL_FRONT_AND_BACK, m_wireframe ? GL_LINE : GL_FILL);

    glClearColor(environment.skyColor.r, environment.skyColor.g, environment.skyColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const FrameData data{
        camera.View(),
        camera.Projection(),
        glm::vec4(camera.position, 1.0f),
        glm::vec4(glm::normalize(light.direction), 0.0f),
        glm::vec4(light.color * light.intensity, 1.0f),
        glm::vec4(light.ambient, 1.0f),
        glm::vec4(glm::pow(environment.skyColor, glm::vec3(2.2f)), environment.fogDensity),
    };
    glNamedBufferSubData(m_frameUbo, 0, sizeof(FrameData), &data);
    glBindBufferBase(GL_UNIFORM_BUFFER, kFrameBinding, m_frameUbo);

    m_litShader->Bind();
}

void Renderer::Submit(const Mesh& mesh, const Material& material, const glm::mat4& model) {
    MOTOR_ASSERT(m_inScene, "Submit fora de BeginScene/EndScene");

    m_litShader->SetMat4("uModel", model);
    m_litShader->SetMat3("uNormalMatrix", glm::inverseTranspose(glm::mat3(model)));
    m_litShader->SetVec4("uColor", material.color);
    m_litShader->SetFloat("uSpecular", material.specular);
    m_litShader->SetFloat("uShininess", material.shininess);
    (material.albedo ? material.albedo : m_whiteTexture)->Bind(0);

    mesh.Draw();

    ++m_stats.drawCalls;
    m_stats.triangles += mesh.IndexCount() / 3;
}

void Renderer::SubmitWireframe(const Mesh& mesh, const glm::mat4& model, const glm::vec4& color) {
    MOTOR_ASSERT(m_inScene, "SubmitWireframe fora de BeginScene/EndScene");

    m_unlitShader->Bind();
    m_unlitShader->SetMat4("uModel", model);
    m_unlitShader->SetVec4("uColor", color);

    // Puxa as linhas um pouco para a frente para não brigarem com as faces no depth buffer.
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(-1.0f, -1.0f);
    glDisable(GL_CULL_FACE);
    mesh.Draw();
    glEnable(GL_CULL_FACE);
    glDisable(GL_POLYGON_OFFSET_LINE);
    glPolygonMode(GL_FRONT_AND_BACK, m_wireframe ? GL_LINE : GL_FILL);

    m_litShader->Bind();
    ++m_stats.drawCalls;
}

void Renderer::EndScene() {
    MOTOR_ASSERT(m_inScene, "EndScene sem BeginScene");
    m_inScene = false;

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    if (m_target) {
        m_target->Resolve();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, m_windowWidth, m_windowHeight);
        m_target = nullptr;
    }
}

} // namespace motor
