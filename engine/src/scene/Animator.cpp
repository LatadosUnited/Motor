#include "motor/scene/Animator.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/assets/Model.h"
#include "motor/core/Log.h"
#include "motor/scene/Scene.h"
#include "motor/scene/SceneRuntime.h"
#include "motor/scene/Script.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace motor {

using json = nlohmann::ordered_json;

// --- Animator --------------------------------------------------------------------

bool Animator::Play(std::string_view clipName, bool loopClip) {
    return Play(model ? std::shared_ptr<const AnimationClip>(model->FindClip(clipName)) : nullptr, loopClip);
}

bool Animator::CrossFade(std::string_view clipName, float duration, bool loopClip) {
    return CrossFade(model ? std::shared_ptr<const AnimationClip>(model->FindClip(clipName)) : nullptr, duration, loopClip);
}

bool Animator::Play(std::shared_ptr<const AnimationClip> clipAsset, bool loopClip) {
    return Start(std::move(clipAsset), 0.0f, loopClip);
}

bool Animator::CrossFade(std::shared_ptr<const AnimationClip> clipAsset, float duration, bool loopClip) {
    return Start(std::move(clipAsset), duration, loopClip);
}

bool Animator::Start(std::shared_ptr<const AnimationClip> clipAsset, float fade, bool loopClip) {
    if (!clipAsset || !model || !model->skeleton) return false;
    // Já é o clipe atual: continua de onde está (Play recomeça só se ele já tiver terminado).
    if (m_current.clip == clipAsset && (fade > 0.0f || !IsFinished())) {
        m_current.loop = loopClip;
        return true;
    }
    const bool blend = fade > 0.0f && m_current.clip;
    m_previous = blend ? std::move(m_current) : Track{};
    m_fade = 0.0f;
    m_fadeDuration = blend ? fade : 0.0f;
    m_current = Track{};
    m_current.binding = BindClip(*clipAsset, *model->skeleton);
    m_current.clip = std::move(clipAsset);
    m_current.loop = loopClip;
    return true;
}

void Animator::Stop() {
    m_current = Track{};
    m_previous = Track{};
    m_fadeDuration = 0.0f;
}

bool Animator::IsPlaying() const {
    return m_current.clip != nullptr;
}

bool Animator::IsFinished() const {
    return m_current.clip && !m_current.loop && m_current.time >= m_current.clip->duration;
}

const std::string& Animator::CurrentClip() const {
    static const std::string kNone;
    return m_current.clip ? m_current.clip->name : kNone;
}

float Animator::ClipTime() const {
    return m_current.clip ? motor::ClipTime(m_current.time, m_current.clip->duration, m_current.loop) : 0.0f;
}

float Animator::ClipLength() const {
    return m_current.clip ? m_current.clip->duration : 0.0f;
}

bool Animator::HasClip(std::string_view clipName) const {
    return model && model->FindClip(clipName);
}

std::vector<std::string> Animator::ClipNames() const {
    std::vector<std::string> names;
    if (model) {
        for (const auto& c : model->clips) names.push_back(c->name);
    }
    return names;
}

void Animator::Sample(const Track& track, std::vector<JointTransform>& pose) const {
    pose = m_poseSkeleton->RestPose();
    if (track.clip) {
        SampleClip(*track.clip, track.binding, motor::ClipTime(track.time, track.clip->duration, track.loop), pose);
    }
}

void Animator::Update(float dt) {
    if (!model || !model->skeleton) {
        m_poseSkeleton.reset();
        m_globals.clear();
        return;
    }
    if (m_poseSkeleton != model->skeleton) {
        // Modelo trocado (Inspector): os clipes eram de outro esqueleto.
        if (m_poseSkeleton) Stop();
        m_poseSkeleton = model->skeleton;
    }

    const float step = dt * speed;
    auto advance = [&](Track& t) {
        if (!t.clip) return;
        t.time = motor::ClipTime(t.time + step, t.clip->duration, t.loop);
    };
    advance(m_current);

    Sample(m_current, m_locals);
    if (m_fadeDuration > 0.0f) {
        advance(m_previous);
        m_fade += std::abs(step);
        const float w = std::clamp(m_fade / m_fadeDuration, 0.0f, 1.0f);
        if (w >= 1.0f) {
            m_previous = Track{};
            m_fadeDuration = 0.0f;
        } else {
            Sample(m_previous, m_fadeLocals);
            for (size_t i = 0; i < m_locals.size(); ++i) m_locals[i] = Blend(m_fadeLocals[i], m_locals[i], w);
        }
    }
    m_globals.resize(m_locals.size());
    m_poseSkeleton->ComputeGlobals(m_locals, m_globals);
}

// --- Campo "model" ------------------------------------------------------------------

// Gravado como { "guid": "...", "path": "Assets/..." } (ou null); quem manda é o GUID.
CustomFieldType FieldTraits<std::shared_ptr<Model>>::Describe() {
    return {"ModelRef",
            [](const void* a, const void* b) {
                return *static_cast<const std::shared_ptr<Model>*>(a) == *static_cast<const std::shared_ptr<Model>*>(b);
            },
            [](const void* v, const AssetLibrary& assets) {
                const auto& model = *static_cast<const std::shared_ptr<Model>*>(v);
                if (!model) return std::string("null");
                const auto ref = assets.RefOf(model.get());
                if (!ref) {
                    log::Warn("Salvar cena: modelo não está no AssetLibrary");
                    return std::string("null");
                }
                json j = {{"guid", ref->guid}};
                if (!ref->name.empty()) j["path"] = ref->name;
                return j.dump();
            },
            [](std::string_view text, void* v, AssetLibrary& assets, std::string* error) {
                auto& model = *static_cast<std::shared_ptr<Model>*>(v);
                const json j = json::parse(text, nullptr, false);
                if (j.is_null()) {
                    model = nullptr;
                    return true;
                }
                std::string guid;
                std::string path;
                if (j.is_string()) {
                    path = j.get<std::string>();
                } else if (j.is_object() && (j.contains("guid") || j.contains("path"))) {
                    guid = j.value("guid", std::string());
                    path = j.value("path", std::string());
                } else {
                    if (error) *error = "esperado caminho \"Assets/...\", objeto {\"path\": \"Assets/...\"} ou null";
                    return false;
                }
                std::string problem;
                std::shared_ptr<Model> loaded = guid.empty() ? nullptr : assets.GetModelByGuid(guid, &problem);
                if (!loaded && !path.empty()) {
                    loaded = assets.GetModel(path, &problem);
                    if (loaded && !guid.empty()) {
                        log::Warn("Cena: GUID {} não encontrado; usando o modelo pelo caminho '{}'", guid, path);
                    }
                }
                if (!loaded) {
                    if (error) {
                        *error = problem.empty() ? "modelo não encontrado" : problem;
                        return false;
                    }
                    log::Warn("Cena: modelo '{}' não carregado: {}", path, problem);
                }
                model = std::move(loaded);
                return true;
            }};
}

// Nome do módulo: depois dos outros no menu "Adicionar componente" (ordem alfabética dos módulos).
MOTOR_COMPONENT_MODULE(SkeletalAnimation) {
    registry.Register<Animator>("Animator", "Animator")
        .Field("model", "Modelo", &Animator::model, {.tooltip = "Modelo glTF (.gltf/.glb) ou FBX (.fbx) com o esqueleto e os clipes"})
        .Field("clip", "Clipe inicial", &Animator::clip, {.tooltip = "Nome do clipe que começa tocando no Play"})
        .Field("playOnStart", "Tocar ao iniciar", &Animator::playOnStart)
        .Field("loop", "Repetir", &Animator::loop, {.tooltip = "O clipe inicial repete"})
        .Field("speed", "Velocidade", &Animator::speed, {.speed = 0.01f, .min = 0.0f, .max = 10.0f, .format = "x%.2f"});
}

// --- Busca e sistema --------------------------------------------------------------

Animator* FindAnimator(Scene& scene, EntityId id) {
    Entity* e = scene.Find(id);
    if (!e) return nullptr;
    if (Animator* a = e->Get<Animator>()) return a;
    for (EntityId other : scene.EntitiesWith<Animator>()) {  // ordem da cena: o descendente mais "alto" vem antes
        if (!scene.IsDescendantOf(other, id)) continue;
        if (Animator* a = scene.Find(other)->Get<Animator>()) return a;
    }
    return nullptr;
}

Animator* Script::GetAnimator() {
    return FindAnimator(GetScene(), GetId());
}

void AnimationSystem::OnStart(SceneRuntime& runtime) {
    Scene& scene = runtime.GetScene();
    for (EntityId id : scene.EntitiesWith<Animator>()) {
        Entity* e = scene.Find(id);
        Animator* a = e ? e->Get<Animator>() : nullptr;
        if (!a || !a->playOnStart || a->clip.empty()) continue;
        if (!a->Play(a->clip, a->loop)) log::Warn("Animator de '{}': o modelo não tem o clipe '{}'", e->name, a->clip);
        a->Update(0.0f);
    }
}

void AnimationSystem::OnUpdate(SceneRuntime& runtime, float dt) {
    Scene& scene = runtime.GetScene();
    for (EntityId id : scene.EntitiesWith<Animator>()) {
        Entity* e = scene.Find(id);
        Animator* a = e ? e->Get<Animator>() : nullptr;
        if (a && scene.IsActiveInHierarchy(id)) a->Update(dt);
    }
}

MOTOR_RUNTIME_SYSTEM(Animacao, AnimationSystem, kSystemOrderAnimation);

} // namespace motor
