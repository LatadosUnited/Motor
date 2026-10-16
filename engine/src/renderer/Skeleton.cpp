#include "motor/renderer/Skeleton.h"

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace motor {

glm::mat4 JointTransform::Matrix() const {
    glm::mat4 m = glm::mat4_cast(rotation);
    m[0] *= scale.x;
    m[1] *= scale.y;
    m[2] *= scale.z;
    m[3] = glm::vec4(translation, 1.0f);
    return m;
}

JointTransform Blend(const JointTransform& a, const JointTransform& b, float t) {
    JointTransform r;
    r.translation = glm::mix(a.translation, b.translation, t);
    r.scale = glm::mix(a.scale, b.scale, t);
    // slerp já pega o caminho mais curto (troca o sinal quando o produto escalar é negativo).
    r.rotation = glm::normalize(glm::slerp(a.rotation, b.rotation, t));
    return r;
}

bool Skeleton::Finalize() {
    restGlobal.assign(joints.size(), glm::mat4(1.0f));
    for (size_t i = 0; i < joints.size(); ++i) {
        const int parent = joints[i].parent;
        if (parent >= static_cast<int>(i)) return false;  // pai precisa vir antes
        const glm::mat4 local = joints[i].rest.Matrix();
        restGlobal[i] = parent < 0 ? local : restGlobal[static_cast<size_t>(parent)] * local;
    }
    return true;
}

int Skeleton::Find(std::string_view name) const {
    for (size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

void Skeleton::ComputeGlobals(std::span<const JointTransform> locals, std::span<glm::mat4> out) const {
    const size_t n = std::min({joints.size(), locals.size(), out.size()});
    for (size_t i = 0; i < n; ++i) {
        const int parent = joints[i].parent;
        const glm::mat4 local = locals[i].Matrix();
        out[i] = parent < 0 ? local : out[static_cast<size_t>(parent)] * local;
    }
}

std::vector<JointTransform> Skeleton::RestPose() const {
    std::vector<JointTransform> pose;
    pose.reserve(joints.size());
    for (const Joint& j : joints) pose.push_back(j.rest);
    return pose;
}

void Skin::JointMatrices(std::span<const glm::mat4> globals, std::vector<glm::mat4>& out) const {
    out.resize(joints.size());
    for (size_t i = 0; i < joints.size(); ++i) {
        const int j = joints[i];
        const glm::mat4 global = j >= 0 && static_cast<size_t>(j) < globals.size() ? globals[static_cast<size_t>(j)]
                                                                                  : glm::mat4(1.0f);
        out[i] = global * (i < inverseBind.size() ? inverseBind[i] : glm::mat4(1.0f));
    }
}

glm::vec3 SkinPosition(const glm::vec3& position, const SkinWeights& weights, std::span<const glm::mat4> jointMatrices) {
    glm::mat4 skin(0.0f);
    for (int k = 0; k < 4; ++k) {
        const float w = weights.weights[k];
        if (w == 0.0f) continue;
        const auto j = static_cast<size_t>(weights.joints[k] + 0.5f);
        if (j < jointMatrices.size()) skin += w * jointMatrices[j];
    }
    return glm::vec3(skin * glm::vec4(position, 1.0f));
}

} // namespace motor
