#include "viewport/NavMeshOverlay.h"

#include "EditorContext.h"

#include <motor/ai/NavMesh.h>
#include <motor/ai/NavSystem.h>
#include <motor/renderer/Mesh.h>
#include <motor/renderer/Renderer.h>
#include <motor/scene/Components.h>
#include <motor/scene/SceneRuntime.h>

#include <glm/gtc/matrix_transform.hpp>

#include <vector>

namespace editor {

namespace {

// Um pouco acima do chão: o navmesh fica na altura do piso (até uma célula de diferença) e brigaria com ele.
constexpr float kLift = 0.04f;
const glm::vec4 kEdgeColor{0.45f, 0.8f, 1.0f, 0.55f};
constexpr ImU32 kPathColor = IM_COL32(255, 214, 64, 255);

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
    motor::NavSystem* nav = runtime ? runtime->GetSystem<motor::NavSystem>() : nullptr;
    if (!nav) return;
    const motor::Scene& scene = ctx.scene;
    auto screen = [&](const glm::vec3& p) -> std::optional<ImVec2> {
        const auto s = project(p + glm::vec3(0.0f, kLift * 2.0f, 0.0f));
        if (!s) return std::nullopt;
        return ImVec2(origin.x + s->x, origin.y + s->y);
    };
    for (motor::EntityId id : scene.EntitiesWith<motor::NavMeshAgent>()) {
        const std::vector<glm::vec3> path = nav->GetPath(id);
        const motor::Entity* e = scene.Find(id);
        if (path.empty() || !e) continue;
        // Dos pés do agente até o destino.
        glm::vec3 from = glm::vec3(scene.WorldMatrix(id)[3]) - glm::vec3(0.0f, e->Get<motor::NavMeshAgent>()->baseOffset, 0.0f);
        for (const glm::vec3& corner : path) {
            const auto a = screen(from), b = screen(corner);
            if (a && b) dl->AddLine(*a, *b, kPathColor, 2.0f);
            from = corner;
        }
        if (const auto end = screen(path.back())) dl->AddCircleFilled(*end, 4.0f, kPathColor);
    }
}

} // namespace editor
