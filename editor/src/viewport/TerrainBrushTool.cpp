#include "viewport/TerrainBrushTool.h"

#include <motor/scene/Terrain.h>

#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace editor {

namespace {

constexpr float kMaxRayDistance = 20000.0f;  // metros: o terreno inteiro de longe
constexpr float kMaxStepSeconds = 0.1f;      // quadro travado não vira um morro de uma vez
constexpr int kCircleSegments = 48;
constexpr ImU32 kCircleColor = IM_COL32(255, 220, 80, 230);
constexpr ImU32 kCircleShadow = IM_COL32(0, 0, 0, 140);

// Terreno selecionado com alturas: o alvo do pincel.
const motor::Terrain* SelectedTerrain(const EditorContext& ctx) {
    if (ctx.selected == kNullEntity) return nullptr;
    const motor::Entity* e = ctx.scene.Find(ctx.selected);
    const motor::Terrain* terrain = e ? e->Get<motor::Terrain>() : nullptr;
    return terrain && terrain->terrain.Data() ? terrain : nullptr;
}

} // namespace

bool TerrainBrushTool::Enabled(const EditorContext& ctx) {
    return ctx.terrainBrush.active && !ctx.playing && SelectedTerrain(ctx);
}

void TerrainBrushTool::Cancel(EditorContext& ctx) {
    if (m_stroke) ctx.PushTerrainStroke(*m_stroke);
    m_stroke.reset();
    m_entity = kNullEntity;
    ctx.terrainBrush.stroking = false;
}

bool TerrainBrushTool::Update(EditorContext& ctx, bool hovered, bool focused, const glm::vec2& mouseUv,
                              const RayFn& ray, const ProjectFn& project, ImDrawList* dl, const ImVec2& origin) {
    if (focused && ctx.terrainBrush.active && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ctx.terrainBrush.active = false;
    if (!Enabled(ctx)) {
        Cancel(ctx);
        return false;
    }
    const motor::Terrain& terrain = *SelectedTerrain(ctx);
    if (m_stroke && (m_entity != ctx.selected || m_stroke->Data() != terrain.terrain.SharedData())) Cancel(ctx);

    // O raio vai para o espaço do terreno (a entidade pode estar movida, girada ou escalada).
    const glm::mat4 world = ctx.scene.WorldMatrix(ctx.selected);
    const glm::mat4 toLocal = glm::inverse(world);
    const auto [rayOrigin, rayDirection] = ray(mouseUv);
    const glm::vec3 localOrigin = glm::vec3(toLocal * glm::vec4(rayOrigin, 1.0f));
    const glm::vec3 localDirection = glm::vec3(toLocal * glm::vec4(rayDirection, 0.0f));
    const motor::TerrainData& data = *terrain.terrain.Data();
    const auto hit = hovered || m_stroke ? data.Raycast(localOrigin, localDirection, kMaxRayDistance) : std::nullopt;

    const motor::TerrainBrush& brush = ctx.terrainBrush.brush;
    if (hit) {
        // Círculo sobre a superfície: cada ponto na altura do terreno (fora dele, na altura do centro).
        ImVec2 points[kCircleSegments];
        int count = 0;
        for (int i = 0; i < kCircleSegments; ++i) {
            const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / kCircleSegments;
            const float x = hit->x + std::cos(angle) * brush.radius, z = hit->z + std::sin(angle) * brush.radius;
            const float y = data.HeightAt(x, z).value_or(hit->y) + 0.05f;
            const auto p = project(glm::vec3(world * glm::vec4(x, y, z, 1.0f)));
            if (!p) {
                count = 0;  // parte atrás da câmera: não desenha um círculo quebrado
                break;
            }
            points[count++] = ImVec2(origin.x + p->x, origin.y + p->y);
        }
        if (count == kCircleSegments) {
            dl->AddPolyline(points, count, kCircleShadow, ImDrawFlags_Closed, 3.5f);
            dl->AddPolyline(points, count, kCircleColor, ImDrawFlags_Closed, 1.5f);
        }
        if (const auto center = project(glm::vec3(world * glm::vec4(*hit, 1.0f)))) {
            dl->AddCircleFilled(ImVec2(origin.x + center->x, origin.y + center->y), 2.5f, kCircleColor);
        }
    }

    if (!m_stroke && hovered && hit && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_stroke.emplace(terrain.terrain.SharedData(), brush);
        m_entity = ctx.selected;
    }
    if (m_stroke) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            Cancel(ctx);
        } else if (hit) {
            m_stroke->Apply(hit->x, hit->z, std::min(ImGui::GetIO().DeltaTime, kMaxStepSeconds));
        }
    }
    ctx.terrainBrush.stroking = m_stroke.has_value();
    return true;
}

} // namespace editor
