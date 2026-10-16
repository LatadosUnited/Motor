#pragma once

// Animação esquelética (Fase 11). O Animator fica na raiz de um modelo glTF (o importador já o coloca no
// prefab) e anima o esqueleto do modelo; as malhas com pele dentro dele (Mesh Renderer de uma malha com pele,
// na mesma entidade ou abaixo) são desenhadas na pose do Animator mais próximo acima delas. Sem Animator, ou
// fora do Play, ficam na pose de repouso.
//
// No Play, o AnimationSystem (sistema do SceneRuntime) começa o clipe inicial e avança as animações a cada
// frame, depois dos scripts. Num script (ver docs/scripts.md, "Animação"):
//
//   if (auto* anim = GetAnimator()) {
//       anim->CrossFade(andando ? "Andando" : "Parado", 0.2f);  // mistura os dois por 0,2 s
//   }

#include "motor/assets/Animation.h"
#include "motor/scene/Component.h"
#include "motor/scene/RuntimeSystem.h"

#include <glm/mat4x4.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

class Scene;
struct Model;
using EntityId = uint32_t;

struct Animator {
    // --- Campos (Inspector e cena) ---
    std::shared_ptr<Model> model;  // modelo glTF de onde vêm o esqueleto e os clipes
    std::string clip;              // clipe que começa tocando no Play (vazio = nenhum)
    bool playOnStart = true;
    bool loop = true;              // o clipe inicial repete
    float speed = 1.0f;            // 1 = velocidade normal; 0 = congela

    // --- Controle (scripts) ---
    // Toca o clipe do modelo com esse nome, trocando na hora. Se ele já estiver tocando, continua de onde está
    // (se já tiver terminado, sem loop, recomeça). false se o modelo não tiver o clipe.
    bool Play(std::string_view clipName, bool loop = true);
    // Troca misturando com o clipe atual durante `duration` segundos (transição suave). Pedir o clipe que já está
    // tocando não faz nada: dá para chamar a cada frame com o clipe desejado.
    bool CrossFade(std::string_view clipName, float duration = 0.2f, bool loop = true);
    // Mesmos, com um clipe de qualquer modelo (AssetLibrary::GetClip): as juntas são achadas pelo nome.
    bool Play(std::shared_ptr<const AnimationClip> clip, bool loop = true);
    bool CrossFade(std::shared_ptr<const AnimationClip> clip, float duration = 0.2f, bool loop = true);
    void Stop();  // para e volta à pose de repouso

    bool IsPlaying() const;                    // há um clipe tocando (mesmo que já no fim, sem loop)
    bool IsFinished() const;                   // clipe sem loop chegou ao fim
    const std::string& CurrentClip() const;    // nome do clipe atual ("" se parado)
    float ClipTime() const;                    // segundos dentro do clipe atual
    float ClipLength() const;                  // duração do clipe atual (0 se parado)
    bool HasClip(std::string_view clipName) const;
    std::vector<std::string> ClipNames() const;  // clipes do modelo

    // Avança `dt` segundos (vezes `speed`) e calcula a pose. Chamado pelo AnimationSystem; dá para chamar
    // direto fora do Play (testes, ferramentas).
    void Update(float dt);

    // Pose calculada no último Update: matriz de cada junta do esqueleto no espaço do modelo (vazia antes).
    std::span<const glm::mat4> GlobalPose() const { return m_globals; }
    const Skeleton* PoseSkeleton() const { return m_poseSkeleton.get(); }

private:
    struct Track {
        std::shared_ptr<const AnimationClip> clip;
        std::vector<int> binding;  // junta do esqueleto para cada canal
        float time = 0.0f;
        bool loop = true;
    };
    bool Start(std::shared_ptr<const AnimationClip> clip, float fade, bool loop);
    void Sample(const Track& track, std::vector<JointTransform>& pose) const;

    Track m_current;
    Track m_previous;  // clipe que está saindo na mistura
    float m_fade = 0.0f;
    float m_fadeDuration = 0.0f;
    std::shared_ptr<const Skeleton> m_poseSkeleton;
    std::vector<JointTransform> m_locals;
    std::vector<JointTransform> m_fadeLocals;
    std::vector<glm::mat4> m_globals;
};

// Campo "model" do Animator: referência ao arquivo .gltf/.glb pelo GUID (com o caminho junto, como plano B).
template <> struct FieldTraits<std::shared_ptr<Model>> {
    static CustomFieldType Describe();
};

// Animator da entidade ou do primeiro descendente que tiver um (nullptr se nenhum).
Animator* FindAnimator(Scene& scene, EntityId id);

// Avança os Animators da cena a cada frame do Play (depois dos scripts) e começa os clipes iniciais no Start.
class AnimationSystem final : public RuntimeSystem {
public:
    void OnStart(SceneRuntime& runtime) override;
    void OnUpdate(SceneRuntime& runtime, float dt) override;
};

} // namespace motor
