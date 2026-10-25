#include "viewport/NavMeshOverlay.h"

#include "EditorContext.h"

#include <motor/ai/NavMesh.h>
#include <motor/ai/NavSystem.h>
#include <motor/renderer/Mesh.h>
#include <motor/renderer/Renderer.h>
#include <motor/scene/Components.h>
#include <motor/scene/SceneRuntime.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

#include <vector>

namespace editor {

namespace {

// Um pouco acima do chão: o navmesh fica na altura do piso (até uma célula de diferença) e brigaria com ele.
constexpr float kLift = 0.04f;
const glm::vec4 kEdgeColor{0.45f, 0.8f, 1.0f, 0.55f};
constexpr ImU32 kPathColor = IM_COL32(255, 214, 64, 255);
constexpr ImU32 kRadiusColor = IM_COL32(120, 230, 140, 220);
constexpr ImU32 kNeighborColor = IM_COL32(255, 150, 90, 160);
constexpr ImU32 kLinkColor = IM_COL32(200, 120, 255, 255);
constexpr ImU32 kAreaColor = IM_COL32(230, 170, 90, 230);

// Arquivo da primeira Superfície de navegação ativa da cena (vazio sem superfície ou sem .mnav).
std::filesystem::path SurfaceFile(const motor::Scene& scene) {
    for (motor::EntityId id : scene.EntitiesWith<motor::NavMeshSurface>()) {
        const motor::Entity* e = scene.Find(id);
        if (e && scene.IsActiveInHierarchy(id)) return e->Get<motor::NavMeshSurface>()->navMesh.file;
    }
    return {};
}

} // namespace

NavMeshOverlay::NavMeshOverlay() {
    m_material.color = {0.25f, 0.65f, 1.0f, 0.35f};
    m_material.emissive = {0.1f, 0.3f, 0.55f};  // visível também na sombra
    m_material.blend = motor::BlendMode::Transparent;
    m_material.doubleSided = true;
}

NavMeshOverlay::~NavMeshOverlay() = default;

void NavMeshOverlay::SetTriangles(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices) {
    if (indices.empty()) {
        m_mesh.reset();
        return;
    }
    std::vector<motor::Vertex> out;
    out.reserve(vertices.size());
    for (const glm::vec3& p : vertices) out.push_back({p + glm::vec3(0.0f, kLift, 0.0f), {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}});
    m_mesh = std::make_shared<motor::Mesh>(out, indices);
}

void NavMeshOverlay::Update(const EditorContext& ctx, motor::SceneRuntime* runtime) {
    // No Play, o navmesh que o jogo está usando (gerado na hora ou lido do arquivo).
    if (motor::NavSystem* nav = runtime ? runtime->GetSystem<motor::NavSystem>() : nullptr) {
        if (m_runtime == runtime && m_version == nav->Version()) return;
        m_runtime = runtime;
        m_version = nav->Version();
        m_file.clear();
        if (const motor::NavMesh* mesh = nav->GetNavMesh()) {
            const motor::NavGeometry g = mesh->DebugTriangles();
            SetTriangles(g.vertices, g.indices);
        } else {
            m_mesh.reset();
        }
        return;
    }

    // Fora do Play, o .mnav da superfície, relido quando muda (Gerar grava por cima do mesmo arquivo).
    m_runtime = nullptr;
    const std::filesystem::path file = SurfaceFile(ctx.scene);
    std::error_code ec;
    const auto time = file.empty() ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(file, ec);
    if (ec) {
        m_file.clear();
        m_mesh.reset();
        return;
    }
    if (file == m_file && time == m_fileTime) return;
    m_file = file;
    m_fileTime = time;
    m_mesh.reset();
    if (file.empty()) return;
    // Arquivo inválido: nada desenhado (o Play avisa no log e gera na hora).
    if (const auto mesh = motor::NavMesh::Load(file)) {
        const motor::NavGeometry g = mesh->DebugTriangles();
        SetTriangles(g.vertices, g.indices);
    }
}

void NavMeshOverlay::Submit(motor::Renderer& renderer) const {
    if (!m_mesh) return;
    const glm::mat4 identity(1.0f);
    renderer.Submit(*m_mesh, m_material, identity, motor::ShadowCasting::Off);
    renderer.SubmitWireframe(*m_mesh, identity, kEdgeColor);
}

void NavMeshOverlay::DrawPaths(const EditorContext& ctx, motor::SceneRuntime* runtime, ImDrawList* dl,
                               const ImVec2& origin,
                               const std::function<std::optional<ImVec2>(const glm::vec3&)>& project) const {
    const motor::Scene& scene = ctx.scene;
    auto screen = [&](const glm::vec3& p) -> std::optional<ImVec2> {
        const auto s = project(p + glm::vec3(0.0f, kLift * 2.0f, 0.0f));
        if (!s) return std::nullopt;
        return ImVec2(origin.x + s->x, origin.y + s->y);
    };
    auto line = [&](const glm::vec3& a, const glm::vec3& b, ImU32 color, float thickness) {
        const auto sa = screen(a), sb = screen(b);
        if (sa && sb) dl->AddLine(*sa, *sb, color, thickness);
    };

    // Ligações (pulos, escadas), no Play e fora dele: o arco que o agente faz, com uma seta na chegada se só vale
    // num sentido.
    for (motor::EntityId id : scene.EntitiesWith<motor::NavMeshLink>()) {
        if (!scene.IsActiveInHierarchy(id)) continue;
        const motor::NavMeshLink& l = *scene.Find(id)->Get<motor::NavMeshLink>();
        const glm::mat4 world = scene.WorldMatrix(id);
        const glm::vec3 a = glm::vec3(world * glm::vec4(l.start, 1.0f)), b = glm::vec3(world * glm::vec4(l.end, 1.0f));
        constexpr int kSteps = 16;
        glm::vec3 previous = a;
        for (int i = 1; i <= kSteps; ++i) {
            const float u = static_cast<float>(i) / kSteps;
            const glm::vec3 p = glm::mix(a, b, u) + glm::vec3(0.0f, l.arcHeight * 4.0f * u * (1.0f - u), 0.0f);
            line(previous, p, kLinkColor, 2.0f);
            previous = p;
        }
        const auto sa = screen(a), sb = screen(b);
        if (sa) dl->AddCircle(*sa, 5.0f, kLinkColor, 0, 2.0f);
        if (sb) dl->AddCircle(*sb, 5.0f, kLinkColor, 0, 2.0f);
        if (!l.bidirectional && sb) {
            // Seta: os dois traços saem da chegada para trás, abertos em volta da direção de quem chega.
            const auto before = screen(glm::mix(a, b, 0.9f) + glm::vec3(0.0f, l.arcHeight * 0.36f, 0.0f));
            if (before) {
                const ImVec2 d(sb->x - before->x, sb->y - before->y);
                const float length = std::max(std::sqrt(d.x * d.x + d.y * d.y), 1e-3f);
                const ImVec2 n(d.x / length * 10.0f, d.y / length * 10.0f);
                dl->AddLine(*sb, ImVec2(sb->x - n.x - n.y * 0.6f, sb->y - n.y + n.x * 0.6f), kLinkColor, 2.0f);
                dl->AddLine(*sb, ImVec2(sb->x - n.x + n.y * 0.6f, sb->y - n.y - n.x * 0.6f), kLinkColor, 2.0f);
            }
        }
    }

    // Áreas de navegação: o contorno da caixa, com o nome e o custo no meio.
    for (motor::EntityId id : scene.EntitiesWith<motor::NavMeshArea>()) {
        if (!scene.IsActiveInHierarchy(id)) continue;
        const motor::NavMeshArea& area = *scene.Find(id)->Get<motor::NavMeshArea>();
        const glm::mat4 world = scene.WorldMatrix(id);
        glm::vec3 corners[8];
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 c(i & 1 ? 0.5f : -0.5f, i & 2 ? 0.5f : -0.5f, i & 4 ? 0.5f : -0.5f);
            corners[i] = glm::vec3(world * glm::vec4(area.center + c * area.size, 1.0f));
        }
        for (int i = 0; i < 8; ++i) {
            for (int bit : {1, 2, 4}) {
                if (!(i & bit)) line(corners[i], corners[i | bit], kAreaColor, 1.5f);
            }
        }
        if (const auto c = screen(glm::vec3(world * glm::vec4(area.center, 1.0f)))) {
            const std::string label = std::format("{} ({}, custo {:.1f})", area.name, area.area, area.cost);
            dl->AddText(ImVec2(c->x - ImGui::CalcTextSize(label.c_str()).x * 0.5f, c->y), kAreaColor, label.c_str());
        }
    }

    motor::NavSystem* nav = runtime ? runtime->GetSystem<motor::NavSystem>() : nullptr;
    for (motor::EntityId id : scene.EntitiesWith<motor::NavMeshAgent>()) {
        if (!scene.IsActiveInHierarchy(id)) continue;
        const motor::NavMeshAgent& agent = *scene.Find(id)->Get<motor::NavMeshAgent>();
        const glm::vec3 feet = motor::NavAgentFeet(scene, id);

        // Raio com que os outros agentes desviam deste, no chão.
        constexpr int kSegments = 24;
        for (int i = 0; i < kSegments; ++i) {
            const float t0 = glm::two_pi<float>() * static_cast<float>(i) / kSegments;
            const float t1 = glm::two_pi<float>() * static_cast<float>(i + 1) / kSegments;
            line(feet + agent.radius * glm::vec3(std::cos(t0), 0.0f, std::sin(t0)),
                 feet + agent.radius * glm::vec3(std::cos(t1), 0.0f, std::sin(t1)), kRadiusColor, 1.5f);
        }
        if (!nav) continue;

        // No Play: de quem ele desvia agora (linha fina até cada vizinho) e o caminho até o destino.
        for (motor::EntityId other : nav->GetNeighbors(id)) {
            if (scene.Find(other)) line(feet, motor::NavAgentFeet(scene, other), kNeighborColor, 1.0f);
        }
        const std::vector<glm::vec3> path = nav->GetPath(id);
        if (path.empty()) continue;
        glm::vec3 from = feet;
        for (const glm::vec3& corner : path) {
            line(from, corner, kPathColor, 2.0f);
            from = corner;
        }
        if (const auto end = screen(path.back())) dl->AddCircleFilled(*end, 4.0f, kPathColor);
    }
}

} // namespace editor
