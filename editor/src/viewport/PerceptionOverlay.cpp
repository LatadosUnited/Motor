#include "viewport/PerceptionOverlay.h"

#include "EditorContext.h"

#include <motor/ai/PerceptionSystem.h>
#include <motor/scene/Components.h>
#include <motor/scene/SceneRuntime.h>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace editor {

namespace {

constexpr ImU32 kConeColor = IM_COL32(240, 240, 200, 170);
constexpr ImU32 kConeSeeingColor = IM_COL32(255, 110, 90, 230);  // o cone de quem vê alguém agora
constexpr ImU32 kSeenColor = IM_COL32(255, 80, 70, 255);
constexpr ImU32 kHearingColor = IM_COL32(110, 200, 255, 200);
constexpr ImU32 kMemoryColor = IM_COL32(170, 170, 170, 220);

} // namespace

void DrawPerception(const EditorContext& ctx, motor::SceneRuntime* runtime, ImDrawList* dl, const ImVec2& origin,
                    const std::function<std::optional<ImVec2>(const glm::vec3&)>& project) {
    const motor::Scene& scene = ctx.scene;
    auto screen = [&](const glm::vec3& p) -> std::optional<ImVec2> {
        const auto s = project(p);
        if (!s) return std::nullopt;
        return ImVec2(origin.x + s->x, origin.y + s->y);
    };
    auto line = [&](const glm::vec3& a, const glm::vec3& b, ImU32 color, float thickness) {
        const auto sa = screen(a), sb = screen(b);
        if (sa && sb) dl->AddLine(*sa, *sb, color, thickness);
    };
    // Círculo no plano horizontal, ou só o arco de `from` a `to` (radianos, a partir do +X).
    auto arc = [&](const glm::vec3& center, float radius, float from, float to, ImU32 color, float thickness) {
        if (radius <= 0.0f) return;
        const int segments = std::clamp(static_cast<int>((to - from) / glm::two_pi<float>() * 48.0f), 4, 48);
        glm::vec3 previous = center + radius * glm::vec3(std::cos(from), 0.0f, std::sin(from));
        for (int i = 1; i <= segments; ++i) {
            const float a = from + (to - from) * static_cast<float>(i) / segments;
            const glm::vec3 p = center + radius * glm::vec3(std::cos(a), 0.0f, std::sin(a));
            line(previous, p, color, thickness);
            previous = p;
        }
    };

    motor::PerceptionSystem* senses = runtime ? runtime->GetSystem<motor::PerceptionSystem>() : nullptr;

    // Até onde chegam os sons de cada alvo com Fonte de áudio (o alcance dos sons vezes o volume).
    for (motor::EntityId id : scene.EntitiesWith<motor::PerceptionTarget>()) {
        if (!scene.IsActiveInHierarchy(id)) continue;
        const motor::Entity& e = *scene.Find(id);
        const motor::AudioSource* source = e.Get<motor::AudioSource>();
        if (!source) continue;
        const float range = e.Get<motor::PerceptionTarget>()->soundRange * source->volume;
        arc(glm::vec3(scene.WorldMatrix(id)[3]), range, 0.0f, glm::two_pi<float>(), kHearingColor, 1.0f);
    }

    for (motor::EntityId id : scene.EntitiesWith<motor::Perception>()) {
        if (!scene.IsActiveInHierarchy(id)) continue;
        const motor::Perception& p = *scene.Find(id)->Get<motor::Perception>();
        const motor::PerceptionEye eye = motor::PerceptionEyeOf(scene, id);
        const std::vector<motor::EntityId> seen = senses ? senses->GetSeen(id) : std::vector<motor::EntityId>{};
        const ImU32 cone = seen.empty() ? kConeColor : kConeSeeingColor;

        // O cone no plano: o leque até o alcance, a partir da frente achatada.
        const float heading = std::atan2(eye.forward.z, eye.forward.x);
        const float half = glm::radians(std::clamp(p.sightAngle, 0.0f, 360.0f) * 0.5f);
        if (p.sightRange > 0.0f) {
            if (half >= glm::pi<float>()) {
                arc(eye.position, p.sightRange, 0.0f, glm::two_pi<float>(), cone, 1.5f);
            } else {
                arc(eye.position, p.sightRange, heading - half, heading + half, cone, 1.5f);
                for (float side : {heading - half, heading + half}) {
                    line(eye.position, eye.position + p.sightRange * glm::vec3(std::cos(side), 0.0f, std::sin(side)),
                         cone, 1.5f);
                }
            }
        }
        arc(eye.position, p.nearRange, 0.0f, glm::two_pi<float>(), cone, 1.0f);
        if (!senses) continue;

        // No Play: quem vê, o que ouviu e onde lembra ter percebido quem não percebe mais.
        for (motor::EntityId target : seen) {
            if (scene.Find(target)) line(eye.position, motor::PerceptionPointOf(scene, target), kSeenColor, 2.0f);
        }
        for (const motor::HeardNoise& noise : senses->GetHeard(id)) {
            line(eye.position, noise.position, kHearingColor, 1.5f);
            if (const auto s = screen(noise.position)) dl->AddCircle(*s, 6.0f, kHearingColor, 0, 1.5f);
        }
        for (const motor::PerceptionMemory& m : senses->GetMemory(id)) {
            if (m.seen || m.heard) continue;
            const auto s = screen(m.position);
            if (!s) continue;
            dl->AddLine(ImVec2(s->x - 5.0f, s->y - 5.0f), ImVec2(s->x + 5.0f, s->y + 5.0f), kMemoryColor, 2.0f);
            dl->AddLine(ImVec2(s->x - 5.0f, s->y + 5.0f), ImVec2(s->x + 5.0f, s->y - 5.0f), kMemoryColor, 2.0f);
            const std::string label = std::format("{:.1f} s", m.age);
            dl->AddText(ImVec2(s->x + 7.0f, s->y - 7.0f), kMemoryColor, label.c_str());
        }
    }
}

} // namespace editor
