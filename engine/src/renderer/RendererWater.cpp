// Renderer: a água (Fase 28, Tarefa 13). Depois dos opacos e do céu, a cena é copiada (cor e profundidade) e cada água
// é desenhada por cima lendo essa cópia: refração, cor pela espessura e espuma saem dela, o reflexo sai do céu.

#include "motor/renderer/Renderer.h"

#include "motor/core/Log.h"
#include "motor/core/Profile.h"
#include "motor/renderer/ShadowMath.h"
#include "renderer/GL.h"
#include "renderer/GpuProfile.h"
#include "renderer/PostProcess.h"

#include <GLFW/glfw3.h>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace motor {

namespace {
// Iguais aos do water.frag (a 7, o céu pré-filtrado, já está vinculada pela cena).
constexpr GLuint kUnitRefractColor = 14, kUnitRefractDepth = 15;
} // namespace

void Renderer::SubmitWater(const Mesh& mesh, const WaterSurface& water, const glm::mat4& model) {
    MOTOR_ASSERT(m_inScene, "SubmitWater fora de BeginScene/EndScene");
    m_waters.push_back({&mesh, water, model});
}

void Renderer::DrawWater(const Frustum& view) {
    MOTOR_PROFILE_GPU("Água");
    // Recorte pela caixa da grade com folga das ondas (a altura e o quanto andam para os lados).
    std::vector<const WaterItem*> visible;
    for (const WaterItem& item : m_waters) {
        const float wave = std::max(item.surface.waveHeight, 0.0f) * 1.5f + 0.01f;
        const Aabb box = TransformAabb({item.mesh->BoundsMin(), item.mesh->BoundsMax()}, item.model);
        if (view.Intersects({box.min - glm::vec3(wave), box.max + glm::vec3(wave)})) visible.push_back(&item);
    }
    m_stats.culled += static_cast<uint32_t>(m_waters.size() - visible.size());
    if (visible.empty()) return;

    m_post->CopyForRefraction();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);  // vista também de baixo (dentro da água)
    glPolygonMode(GL_FRONT_AND_BACK, m_wireframe ? GL_LINE : GL_FILL);
    m_waterShader->Bind();
    glBindTextureUnit(kUnitRefractColor, m_post->RefractionColor());
    glBindTextureUnit(kUnitRefractDepth, m_post->RefractionDepth());
    // O relógio do GLFW (o mesmo do quadro): as ondas andam também com o jogo parado, como o céu do editor.
    const float time = static_cast<float>(std::fmod(glfwGetTime(), 3600.0));
    const WaterUniforms& u = m_waterUniforms;
    m_waterShader->SetFloat(u.time, time);
    for (const WaterItem* item : visible) {
        const WaterSurface& w = item->surface;
        m_waterShader->SetMat4(u.model, ModelRelativeToOrigin(item->model, m_camera.position));
        m_waterShader->SetVec4(u.wave, {w.waveHeight, w.waveLength, w.waveSpeed, glm::radians(w.waveDirection)});
        m_waterShader->SetVec4(u.shallow, glm::vec4(w.shallowColor, w.depthFade));
        m_waterShader->SetVec4(u.deep, glm::vec4(w.deepColor, w.refraction));
        m_waterShader->SetVec4(u.water, {w.reflection, w.foam, time, w.waveLength});
        item->mesh->Draw();
        ++m_stats.drawCalls;
        m_stats.triangles += static_cast<uint32_t>(item->mesh->Indices().size() / 3);
    }
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}

} // namespace motor
