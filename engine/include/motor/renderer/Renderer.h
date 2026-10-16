#pragma once

#include "motor/renderer/Camera.h"
#include "motor/renderer/Material.h"
#include "motor/renderer/Mesh.h"
#include "motor/renderer/Shader.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>

namespace motor {

class Framebuffer;

struct DirectionalLight {
    glm::vec3 direction{-0.4f, -1.0f, -0.3f};
    glm::vec3 color{1.0f, 0.97f, 0.9f};
    float intensity = 1.0f;
    glm::vec3 ambient{0.12f, 0.14f, 0.18f};
};

struct Environment {
    glm::vec3 skyColor{0.53f, 0.70f, 0.88f};  // sRGB; também é a cor da neblina
    float fogDensity = 0.012f;
};

struct RenderStats {
    uint32_t drawCalls = 0;
    uint32_t triangles = 0;
};

class Renderer {
public:
    Renderer();  // exige um contexto OpenGL atual (criado pela Window); carrega as funções do OpenGL
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Chamado pela Application no início de cada frame: limpa a janela.
    void BeginFrame(int width, int height);
    void SetClearColor(const glm::vec4& color) { m_clearColor = color; }

    void SetWireframe(bool enabled) { m_wireframe = enabled; }
    bool IsWireframe() const { return m_wireframe; }

    // target == nullptr desenha na janela. A cena começa limpando o alvo com a cor do céu.
    void BeginScene(const Camera& camera, const DirectionalLight& light,
                    const Environment& environment = {}, const Framebuffer* target = nullptr);
    void Submit(const Mesh& mesh, const Material& material, const glm::mat4& model);
    void SubmitWireframe(const Mesh& mesh, const glm::mat4& model, const glm::vec4& color);  // sem iluminação
    void EndScene();

    const RenderStats& GetStats() const { return m_stats; }

private:
    std::unique_ptr<Shader> m_litShader;
    std::unique_ptr<Shader> m_unlitShader;
    std::shared_ptr<Texture2D> m_whiteTexture;
    unsigned int m_frameUbo = 0;
    glm::vec4 m_clearColor{0.08f, 0.09f, 0.10f, 1.0f};
    int m_windowWidth = 0;
    int m_windowHeight = 0;
    const Framebuffer* m_target = nullptr;
    bool m_wireframe = false;
    bool m_inScene = false;
    RenderStats m_stats;
};

} // namespace motor
