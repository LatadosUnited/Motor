#include "motor/renderer/ShadowMath.h"

#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace motor {

Aabb TransformAabb(const Aabb& box, const glm::mat4& m) {
    // Arvo: centro transformado + meia-extensão pelos valores absolutos da matriz.
    const glm::vec3 center = (box.min + box.max) * 0.5f;
    const glm::vec3 half = (box.max - box.min) * 0.5f;
    const glm::vec3 c = glm::vec3(m * glm::vec4(center, 1.0f));
    glm::vec3 h(0.0f);
    for (int col = 0; col < 3; ++col) h += glm::abs(glm::vec3(m[col])) * half[col];
    return {c - h, c + h};
}

glm::mat3 NormalMatrix(const glm::mat4& model) {
    const glm::vec3 a(model[0]), b(model[1]), c(model[2]);
    glm::mat3 cof(glm::cross(b, c), glm::cross(c, a), glm::cross(a, b));
    if (glm::dot(a, cof[0]) < 0.0f) cof = -cof;  // escala negativa (espelho): a inversa transposta vira o sinal
    return cof;
}

Frustum Frustum::FromMatrix(const glm::mat4& vp) {
    // Gribb-Hartmann: cada plano é uma linha da matriz somada/subtraída da quarta.
    const glm::vec4 row0{vp[0][0], vp[1][0], vp[2][0], vp[3][0]};
    const glm::vec4 row1{vp[0][1], vp[1][1], vp[2][1], vp[3][1]};
    const glm::vec4 row2{vp[0][2], vp[1][2], vp[2][2], vp[3][2]};
    const glm::vec4 row3{vp[0][3], vp[1][3], vp[2][3], vp[3][3]};
    Frustum f;
    f.planes = {row3 + row0, row3 - row0, row3 + row1, row3 - row1, row3 + row2, row3 - row2};
    for (glm::vec4& p : f.planes) {
        const float len = glm::length(glm::vec3(p));
        if (len > 0.0f) p /= len;
    }
    return f;
}

bool Frustum::Intersects(const Aabb& box) const {
    for (const glm::vec4& p : planes) {
        // Canto da caixa mais "para dentro" do plano.
        const glm::vec3 v{p.x >= 0.0f ? box.max.x : box.min.x, p.y >= 0.0f ? box.max.y : box.min.y,
                          p.z >= 0.0f ? box.max.z : box.min.z};
        if (glm::dot(glm::vec3(p), v) + p.w < 0.0f) return false;
    }
    return true;
}

bool Frustum::IntersectsSphere(const glm::vec3& center, float radius) const {
    for (const glm::vec4& p : planes) {
        if (glm::dot(glm::vec3(p), center) + p.w < -radius) return false;
    }
    return true;
}

std::vector<float> CascadeSplits(float nearPlane, float farPlane, int count, float lambda) {
    count = std::max(count, 1);
    nearPlane = std::max(nearPlane, 1e-3f);
    farPlane = std::max(farPlane, nearPlane * 1.01f);
    std::vector<float> splits(static_cast<size_t>(count));
    for (int i = 1; i <= count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count);
        const float logSplit = nearPlane * std::pow(farPlane / nearPlane, t);
        const float uniform = nearPlane + (farPlane - nearPlane) * t;
        splits[static_cast<size_t>(i - 1)] = lambda * logSplit + (1.0f - lambda) * uniform;
    }
    splits.back() = farPlane;
    return splits;
}

std::array<glm::vec3, 8> FrustumSliceCorners(const Camera& camera, float nearDist, float farDist) {
    const glm::vec3 forward = camera.Forward(), right = camera.Right(), up = camera.Up();
    const float tanY = std::tan(glm::radians(camera.fovY) * 0.5f);
    const float tanX = tanY * camera.aspect;
    std::array<glm::vec3, 8> corners{};
    const float dists[2] = {nearDist, farDist};
    for (int i = 0; i < 2; ++i) {
        const glm::vec3 c = camera.position + forward * dists[i];
        const glm::vec3 x = right * (tanX * dists[i]);
        const glm::vec3 y = up * (tanY * dists[i]);
        corners[static_cast<size_t>(i * 4 + 0)] = c - x - y;
        corners[static_cast<size_t>(i * 4 + 1)] = c + x - y;
        corners[static_cast<size_t>(i * 4 + 2)] = c + x + y;
        corners[static_cast<size_t>(i * 4 + 3)] = c - x + y;
    }
    return corners;
}

Cascade ComputeCascade(const Camera& camera, float nearDist, float farDist, const glm::vec3& lightDir, int resolution,
                       float casterDepth, float margin) {
    const auto corners = FrustumSliceCorners(camera, nearDist, farDist);
    glm::vec3 center(0.0f);
    for (const glm::vec3& c : corners) center += c;
    center /= 8.0f;
    float radius = 0.0f;
    for (const glm::vec3& c : corners) radius = std::max(radius, glm::length(c - center));
    radius *= std::max(margin, 1.0f);
    // Arredondado para cima: pequenas variações de ponto flutuante não mudam o tamanho do texel.
    radius = std::ceil(radius * 16.0f) / 16.0f;

    const glm::vec3 dir = glm::length(lightDir) > 1e-6f ? glm::normalize(lightDir) : glm::vec3(0.0f, -1.0f, 0.0f);
    const glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    // Orientação fixa (na origem): só a translação muda de um quadro para o outro, em passos de um texel.
    const glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), dir, up);

    const float texel = 2.0f * radius / static_cast<float>(std::max(resolution, 1));
    glm::vec3 lc = glm::vec3(lightView * glm::vec4(center, 1.0f));
    lc.x = std::floor(lc.x / texel) * texel;
    lc.y = std::floor(lc.y / texel) * texel;

    // Vista olhando para -Z: um ponto à frente tem z negativo; perto/longe são distâncias.
    const float zNear = -lc.z - radius - casterDepth;
    const float zFar = -lc.z + radius;
    const glm::mat4 projection = glm::ortho(lc.x - radius, lc.x + radius, lc.y - radius, lc.y + radius, zNear, zFar);

    Cascade cascade;
    cascade.viewProjection = projection * lightView;
    cascade.texelSize = texel;
    cascade.center = center;
    cascade.radius = radius;
    return cascade;
}

bool CascadeCovers(const Cascade& cached, const Cascade& fresh) {
    return cached.radius > 0.0f && glm::length(fresh.center - cached.center) + fresh.radius <= cached.radius;
}

glm::mat4 CubeFaceView(const glm::vec3& position, int face) {
    // Tabela das faces do cubemap do OpenGL (direção e "para cima" de cada uma).
    static const glm::vec3 kTargets[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const glm::vec3 kUps[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
    face = std::clamp(face, 0, 5);
    return glm::lookAt(position, position + kTargets[face], kUps[face]);
}

glm::mat4 CubeFaceProjection(float nearPlane, float farPlane) {
    return glm::perspective(glm::radians(90.0f), 1.0f, nearPlane, farPlane);
}

bool SpotCoversCubeFace(const glm::vec3& direction, float outerAngle, int face) {
    const float length = glm::length(direction);
    if (length <= 0.0f || face < 0 || face > 5) return true;
    const glm::vec3 d = direction / length;
    // A face é o cone de base quadrada entre 4 planos (eixo ± cada um dos outros dois eixos). O spot fica fora se o
    // cone inteiro estiver do lado de fora de um deles. 1° a mais cobre o PCF e a costura entre as faces.
    const float sinAngle = std::sin(glm::radians(std::clamp(outerAngle + 1.0f, 0.0f, 90.0f)));
    const int axis = face / 2;
    const float sign = face % 2 == 0 ? 1.0f : -1.0f;
    for (int other = 0; other < 3; ++other) {
        if (other == axis) continue;
        for (const float s : {1.0f, -1.0f}) {
            glm::vec3 normal(0.0f);
            normal[axis] = sign;
            normal[other] = s;
            if (glm::dot(d, normal) * 0.70710678f < -sinAngle) return false;
        }
    }
    return true;
}

} // namespace motor
