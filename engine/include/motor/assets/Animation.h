#pragma once

// Clipes de animação esquelética (Fase 11), lidos do glTF. Só dados e contas na CPU (dá para testar sem janela):
// o componente Animator (motor/scene/Animator.h) usa estas funções para montar a pose a cada frame.

#include "motor/renderer/Skeleton.h"

#include <glm/vec4.hpp>

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace motor {

enum class AnimationPath { Translation, Rotation, Scale };
enum class AnimationInterpolation { Linear, Step, CubicSpline };

// Uma propriedade (posição, rotação ou escala) de uma junta ao longo do tempo.
struct AnimationChannel {
    std::string joint;    // nome da junta (clipes de outro arquivo acham a junta por ele)
    int jointIndex = -1;  // índice no esqueleto de onde o clipe veio
    AnimationPath path = AnimationPath::Translation;
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    std::vector<float> times;  // segundos, crescentes
    // Um valor por tempo (xyz para posição/escala; quatérnio xyzw para rotação). CubicSpline: três por tempo
    // (tangente de entrada, valor, tangente de saída), como no glTF.
    std::vector<glm::vec4> values;
};

struct AnimationClip {
    std::string name;
    float duration = 0.0f;  // segundos (o último tempo dos canais)
    std::vector<AnimationChannel> channels;
    std::shared_ptr<const Skeleton> skeleton;  // esqueleto do arquivo de onde o clipe veio
};

// Junta de `skeleton` para cada canal (-1 = a junta não existe nele). O mesmo esqueleto do clipe usa o índice;
// outro esqueleto (clipe de outro arquivo com o mesmo rig), o nome.
std::vector<int> BindClip(const AnimationClip& clip, const Skeleton& skeleton);

// Tempo dentro do clipe: com loop, dá a volta; sem loop, para no fim.
float ClipTime(float time, float duration, bool loop);

// Escreve em `pose` (uma JointTransform por junta do esqueleto) os valores do clipe no tempo `time` (já dentro
// do clipe, ver ClipTime). Juntas sem canal ficam como estavam (comece da pose de repouso).
void SampleClip(const AnimationClip& clip, std::span<const int> binding, float time, std::span<JointTransform> pose);

} // namespace motor
