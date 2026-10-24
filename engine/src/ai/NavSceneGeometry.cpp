// Triângulos de onde o navmesh nasce: os Colliders estáticos da cena, já no mundo.

#include "motor/ai/NavSystem.h"

#include "motor/renderer/Mesh.h"
#include "motor/scene/Components.h"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>

#include <cmath>

namespace motor {

namespace {

constexpr int kRoundSegments = 16;  // cilindro e esfera: o navmesh não precisa de mais
constexpr int kSphereRings = 8;

// Forma convexa: cada triângulo sai virado para fora do centro dela (o sentido decide qual face vira chão).
struct ConvexWriter {
    NavGeometry& out;
    glm::mat4 world;
    glm::vec3 center;  // centro da forma, no mundo

    void Triangle(const glm::vec3& la, const glm::vec3& lb, const glm::vec3& lc) {
        const glm::vec3 a = Point(la), b = Point(lb), c = Point(lc);
        const glm::vec3 normal = glm::cross(b - a, c - a);
        if (glm::dot(normal, (a + b + c) / 3.0f - center) < 0.0f) out.AddTriangle(a, c, b);
        else out.AddTriangle(a, b, c);
    }
    void Quad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d) {
        Triangle(a, b, c);
        Triangle(a, c, d);
    }
    glm::vec3 Point(const glm::vec3& local) const { return glm::vec3(world * glm::vec4(local, 1.0f)); }
};

void AddBox(ConvexWriter& w, const glm::vec3& center, const glm::vec3& size) {
    const glm::vec3 h = size * 0.5f;
    glm::vec3 p[8];
    for (int i = 0; i < 8; ++i) {
        p[i] = center + glm::vec3(i & 1 ? h.x : -h.x, i & 2 ? h.y : -h.y, i & 4 ? h.z : -h.z);
    }
    w.Quad(p[0], p[1], p[3], p[2]);  // -Z
    w.Quad(p[4], p[5], p[7], p[6]);  // +Z
    w.Quad(p[0], p[1], p[5], p[4]);  // -Y
    w.Quad(p[2], p[3], p[7], p[6]);  // +Y
    w.Quad(p[0], p[2], p[6], p[4]);  // -X
    w.Quad(p[1], p[3], p[7], p[5]);  // +X
}

void AddCylinder(ConvexWriter& w, const glm::vec3& center, float radius, float height) {
    const float half = height * 0.5f;
    auto ring = [&](int i, float y) {
        const float a = glm::two_pi<float>() * static_cast<float>(i) / kRoundSegments;
        return center + glm::vec3(std::cos(a) * radius, y, std::sin(a) * radius);
    };
    const glm::vec3 top = center + glm::vec3(0.0f, half, 0.0f), bottom = center - glm::vec3(0.0f, half, 0.0f);
    for (int i = 0; i < kRoundSegments; ++i) {
        const int j = (i + 1) % kRoundSegments;
        w.Quad(ring(i, -half), ring(j, -half), ring(j, half), ring(i, half));
        w.Triangle(top, ring(i, half), ring(j, half));
        w.Triangle(bottom, ring(j, -half), ring(i, -half));
    }
}

void AddSphere(ConvexWriter& w, const glm::vec3& center, float radius) {
    auto point = [&](int ring, int segment) {
        const float polar = glm::pi<float>() * static_cast<float>(ring) / kSphereRings;
        const float a = glm::two_pi<float>() * static_cast<float>(segment) / kRoundSegments;
        return center + radius * glm::vec3(std::sin(polar) * std::cos(a), std::cos(polar), std::sin(polar) * std::sin(a));
    };
    for (int r = 0; r < kSphereRings; ++r) {
        for (int s = 0; s < kRoundSegments; ++s) {
            const int t = (s + 1) % kRoundSegments;
            // Nos polos o quadrilátero degenera em triângulo: o triângulo nulo é só descartado.
            if (r > 0) w.Triangle(point(r, s), point(r, t), point(r + 1, t));
            if (r < kSphereRings - 1) w.Triangle(point(r, s), point(r + 1, t), point(r + 1, s));
        }
    }
}

void AddMesh(NavGeometry& out, const glm::mat4& world, const glm::vec3& offset, const Mesh& mesh) {
    const auto& positions = mesh.Positions();
    const auto& indices = mesh.Indices();
    // Escala negativa espelha a malha e inverte o sentido dos triângulos: desinverte.
    const bool mirrored = glm::determinant(glm::mat3(world)) < 0.0f;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        if (indices[i] >= positions.size() || indices[i + 1] >= positions.size() || indices[i + 2] >= positions.size()) {
            continue;
        }
        const glm::vec3 a = glm::vec3(world * glm::vec4(positions[indices[i]] + offset, 1.0f));
        const glm::vec3 b = glm::vec3(world * glm::vec4(positions[indices[i + 1]] + offset, 1.0f));
        const glm::vec3 c = glm::vec3(world * glm::vec4(positions[indices[i + 2]] + offset, 1.0f));
        if (mirrored) out.AddTriangle(a, c, b);
        else out.AddTriangle(a, b, c);
    }
}

} // namespace

NavGeometry CollectNavGeometry(const Scene& scene) {
    NavGeometry out;
    for (EntityId id : scene.EntitiesWith<ColliderComponent>()) {
        const Entity* e = scene.Find(id);
        if (!e || !scene.IsActiveInHierarchy(id)) continue;
        const ColliderComponent* c = e->Get<ColliderComponent>();
        // Só o cenário parado: o que a física move, os gatilhos e quem anda (agentes, personagens) ficam de fora.
        if (!c || c->isTrigger || e->Has<RigidbodyComponent>() || e->Has<NavMeshAgent>() ||
            e->Has<CharacterControllerComponent>()) {
            continue;
        }
        const glm::mat4 world = scene.WorldMatrix(id);
        ConvexWriter writer{out, world, glm::vec3(world * glm::vec4(c->center, 1.0f))};
        switch (c->shape) {
            case ColliderShape::Box: AddBox(writer, c->center, c->size); break;
            case ColliderShape::Sphere: AddSphere(writer, c->center, c->radius); break;
            // A cápsula vira o cilindro que a contém: para o navmesh, a diferença está só nas pontas.
            case ColliderShape::Capsule:
            case ColliderShape::Cylinder: AddCylinder(writer, c->center, c->radius, c->height); break;
            case ColliderShape::Mesh: {
                const MeshRenderer* renderer = e->Get<MeshRenderer>();
                if (renderer && renderer->mesh) AddMesh(out, world, c->center, *renderer->mesh);
                break;
            }
        }
    }
    return out;
}

} // namespace motor
