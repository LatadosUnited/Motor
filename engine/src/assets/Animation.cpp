#include "motor/assets/Animation.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace motor {

namespace {
glm::quat ToQuat(const glm::vec4& v) {
    return glm::quat(v.w, v.x, v.y, v.z);  // glTF guarda xyzw; o construtor do GLM é (w, x, y, z)
}

// Valor do canal no tempo t: chave anterior/seguinte e quanto andou entre elas.
glm::vec4 Sample(const AnimationChannel& c, float t) {
    const size_t n = c.times.size();
    const bool cubic = c.interpolation == AnimationInterpolation::CubicSpline;
    const size_t stride = cubic ? 3 : 1;
    if (n == 0 || c.values.size() < n * stride) return glm::vec4(0.0f);
    auto value = [&](size_t k) { return c.values[k * stride + (cubic ? 1 : 0)]; };

    if (n == 1 || t <= c.times.front()) return value(0);
    if (t >= c.times.back()) return value(n - 1);

    // Primeira chave com tempo > t.
    const size_t next = static_cast<size_t>(std::upper_bound(c.times.begin(), c.times.end(), t) - c.times.begin());
    const size_t prev = next - 1;
    const float t0 = c.times[prev], t1 = c.times[next];
    const float span = t1 - t0;
    const float u = span > 0.0f ? (t - t0) / span : 0.0f;

    switch (c.interpolation) {
        case AnimationInterpolation::Step: return value(prev);
        case AnimationInterpolation::CubicSpline: {
            // Hermite com as tangentes do arquivo (escaladas pela duração do trecho), como no anexo do glTF.
            const glm::vec4 p0 = value(prev), p1 = value(next);
            const glm::vec4 m0 = c.values[prev * 3 + 2] * span;  // tangente de saída de prev
            const glm::vec4 m1 = c.values[next * 3 + 0] * span;  // tangente de entrada de next
            const float u2 = u * u, u3 = u2 * u;
            return (2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * p1 + (u3 - u2) * m1;
        }
        case AnimationInterpolation::Linear:
            if (c.path == AnimationPath::Rotation) {
                const glm::quat q = glm::slerp(ToQuat(value(prev)), ToQuat(value(next)), u);
                return {q.x, q.y, q.z, q.w};
            }
            return glm::mix(value(prev), value(next), u);
    }
    return value(prev);
}
} // namespace

std::vector<int> BindClip(const AnimationClip& clip, const Skeleton& skeleton) {
    std::vector<int> binding;
    binding.reserve(clip.channels.size());
    const bool same = clip.skeleton.get() == &skeleton;
    for (const AnimationChannel& c : clip.channels) {
        int joint = -1;
        if (same && c.jointIndex >= 0 && c.jointIndex < static_cast<int>(skeleton.joints.size())) {
            joint = c.jointIndex;
        } else {
            joint = skeleton.Find(c.joint);
        }
        binding.push_back(joint);
    }
    return binding;
}

float ClipTime(float time, float duration, bool loop) {
    if (duration <= 0.0f) return 0.0f;
    if (!loop) return std::clamp(time, 0.0f, duration);
    const float wrapped = std::fmod(time, duration);
    return wrapped < 0.0f ? wrapped + duration : wrapped;
}

void SampleClip(const AnimationClip& clip, std::span<const int> binding, float time, std::span<JointTransform> pose) {
    for (size_t i = 0; i < clip.channels.size() && i < binding.size(); ++i) {
        const int joint = binding[i];
        if (joint < 0 || static_cast<size_t>(joint) >= pose.size()) continue;
        const AnimationChannel& c = clip.channels[i];
        const glm::vec4 v = Sample(c, time);
        JointTransform& j = pose[static_cast<size_t>(joint)];
        switch (c.path) {
            case AnimationPath::Translation: j.translation = glm::vec3(v); break;
            case AnimationPath::Scale: j.scale = glm::vec3(v); break;
            case AnimationPath::Rotation: {
                const glm::quat q = ToQuat(v);
                const float len = glm::length(q);
                if (len > 1e-6f) j.rotation = q / len;
                break;
            }
        }
    }
}

} // namespace motor
