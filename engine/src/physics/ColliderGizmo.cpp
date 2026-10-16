#include "motor/physics/ColliderGizmo.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <cmath>

namespace motor {

namespace {

constexpr int kCircleSegments = 24;
constexpr float kPi = 3.14159265358979f;

// Origem e eixos (já girados, sem escala) da entidade no mundo; a escala vai para as medidas.
struct Frame {
    glm::vec3 origin{0.0f};
    glm::mat3 axes{1.0f};
    glm::vec3 scale{1.0f};

    glm::vec3 operator()(const glm::vec3& local) const { return origin + axes * local; }
};

Frame MakeFrame(const glm::mat4& world, bool keepRotation) {
    Frame f;
    f.origin = glm::vec3(world[3]);
    for (int i = 0; i < 3; ++i) {
        const glm::vec3 axis(world[i]);
        const float length = glm::length(axis);
        f.scale[i] = std::max(length, 1e-4f);
        if (keepRotation && length > 1e-8f) f.axes[i] = axis / length;
    }
    return f;
}

void Line(const Frame& f, const glm::vec3& a, const glm::vec3& b, std::vector<GizmoLine>& out) {
    out.push_back({f(a), f(b)});
}

void Box(const Frame& f, const glm::vec3& center, const glm::vec3& half, std::vector<GizmoLine>& out) {
    glm::vec3 corners[8];
    for (int i = 0; i < 8; ++i) {
        corners[i] = center + glm::vec3((i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z);
    }
    // Arestas: pares de cantos que diferem em um só bit.
    for (int i = 0; i < 8; ++i) {
        for (int bit = 1; bit < 8; bit <<= 1) {
            if (!(i & bit)) Line(f, corners[i], corners[i | bit], out);
        }
    }
}

// Arco no plano dos eixos u e v (índices 0..2), de `from` a `to` radianos.
void Arc(const Frame& f, const glm::vec3& center, float radius, int u, int v, float from, float to, int segments,
         std::vector<GizmoLine>& out) {
    glm::vec3 previous = center;
    for (int i = 0; i <= segments; ++i) {
        const float angle = from + (to - from) * static_cast<float>(i) / static_cast<float>(segments);
        glm::vec3 p = center;
        p[u] += std::cos(angle) * radius;
        p[v] += std::sin(angle) * radius;
        if (i > 0) Line(f, previous, p, out);
        previous = p;
    }
}

void Sphere(const Frame& f, const glm::vec3& center, float radius, std::vector<GizmoLine>& out) {
    Arc(f, center, radius, 0, 1, 0.0f, 2.0f * kPi, kCircleSegments, out);
    Arc(f, center, radius, 0, 2, 0.0f, 2.0f * kPi, kCircleSegments, out);
    Arc(f, center, radius, 1, 2, 0.0f, 2.0f * kPi, kCircleSegments, out);
}

void Capsule(const Frame& f, const glm::vec3& center, float radius, float halfCylinder, std::vector<GizmoLine>& out) {
    if (halfCylinder <= 0.001f) {
        Sphere(f, center, radius, out);
        return;
    }
    const glm::vec3 top = center + glm::vec3(0.0f, halfCylinder, 0.0f);
    const glm::vec3 bottom = center - glm::vec3(0.0f, halfCylinder, 0.0f);
    Arc(f, top, radius, 0, 2, 0.0f, 2.0f * kPi, kCircleSegments, out);
    Arc(f, bottom, radius, 0, 2, 0.0f, 2.0f * kPi, kCircleSegments, out);
    for (int i = 0; i < 4; ++i) {
        const float angle = static_cast<float>(i) * 0.5f * kPi;
        const glm::vec3 side(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
        Line(f, top + side, bottom + side, out);
    }
    // Pontas: meias-voltas nos planos XY e ZY.
    const int half = kCircleSegments / 2;
    Arc(f, top, radius, 0, 1, 0.0f, kPi, half, out);
    Arc(f, bottom, radius, 0, 1, kPi, 2.0f * kPi, half, out);
    Arc(f, top, radius, 2, 1, 0.0f, kPi, half, out);
    Arc(f, bottom, radius, 2, 1, kPi, 2.0f * kPi, half, out);
}

float MaxComponent(const glm::vec3& v) { return std::max(v.x, std::max(v.y, v.z)); }

} // namespace

void AppendColliderLines(const ColliderComponent& c, const glm::mat4& world, std::vector<GizmoLine>& out,
                         const glm::vec3* meshMin, const glm::vec3* meshMax) {
    const Frame f = MakeFrame(world, true);
    const glm::vec3& s = f.scale;
    const glm::vec3 center = c.center * s;
    switch (c.shape) {
        case ColliderShape::Box: Box(f, center, glm::max(c.size * s * 0.5f, glm::vec3(0.005f)), out); break;
        case ColliderShape::Sphere: Sphere(f, center, std::max(c.radius * MaxComponent(s), 0.005f), out); break;
        case ColliderShape::Capsule: {
            const float radius = std::max(c.radius * std::max(s.x, s.z), 0.005f);
            Capsule(f, center, radius, c.height * s.y * 0.5f - radius, out);
            break;
        }
        case ColliderShape::Mesh: {
            if (meshMin && meshMax) {
                Box(f, center + (*meshMin + *meshMax) * 0.5f * s, glm::max((*meshMax - *meshMin) * 0.5f * s, glm::vec3(0.005f)), out);
            } else {
                Box(f, center, s * 0.5f, out);
            }
            break;
        }
    }
}

void AppendCharacterLines(const CharacterControllerComponent& controller, const glm::mat4& world,
                          std::vector<GizmoLine>& out) {
    // O personagem não gira nem escala com a entidade: só a posição conta.
    Frame f;
    f.origin = glm::vec3(world[3]);
    const float radius = std::max(controller.radius, 0.01f);
    Capsule(f, glm::vec3(0.0f), radius, std::max(controller.height * 0.5f - radius, 0.01f), out);
}

} // namespace motor
