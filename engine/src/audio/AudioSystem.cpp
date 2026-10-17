#include "motor/audio/AudioSystem.h"

#include "motor/core/Log.h"
#include "motor/core/Paths.h"
#include "motor/scene/AudioListener.h"
#include "motor/scene/AudioSource.h"
#include "motor/scene/Scene.h"
#include "motor/scene/SceneRuntime.h"

#include <miniaudio.h>

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>

namespace motor {

MOTOR_RUNTIME_SYSTEM(Audio, AudioSystem, kSystemOrderAudio);

namespace {
constexpr ma_uint32 kChannels = 2;
constexpr ma_uint32 kSampleRateWithoutDevice = 48000;
// Arquivos maiores que isso (música) são lidos aos poucos durante o jogo; os menores, decodificados inteiros
// ao tocar a primeira vez (efeitos curtos sem atraso e sem custo no meio do jogo).
constexpr std::uintmax_t kStreamAbove = 2 * 1024 * 1024;

// Queda do volume com a distância do modelo "inverso" do miniaudio (o padrão), com rolloff 1.
float DistanceGain(float distance, float minDistance, float maxDistance) {
    const float d = std::clamp(distance, minDistance, std::max(minDistance, maxDistance));
    return minDistance / (minDistance + (d - minDistance));
}
} // namespace

AudioSettings& AudioSettings::Global() {
    static AudioSettings settings;
    return settings;
}

struct AudioSystem::Voice {
    std::filesystem::path file;
    std::string clip;
    ma_sound sound{};
    bool initialized = false;      // ma_sound_init deu certo (só então pode ter ma_sound_uninit)
    bool resumeOnUnpause = false;  // estava tocando quando o jogo pausou
    bool spatial = false;
    float distance = 0.0f;
    float gain = 1.0f;

    ~Voice() {
        if (initialized) ma_sound_uninit(&sound);
    }
};

struct AudioSystem::Impl {
    ma_engine engine{};
    bool engineOk = false;
    bool hasDevice = false;
    AudioSettings settings;
    bool initTried = false;
    std::map<EntityId, std::unique_ptr<Voice>> voices;
    std::set<EntityId> seen;               // fontes ativas que já passaram pelo "tocar ao iniciar"
    std::set<std::filesystem::path> failed;  // arquivos que não abriram (avisa uma vez por Play)
    std::atomic<float> level[kChannels] = {0.0f, 0.0f};

    // Chamado depois de cada bloco misturado (na thread de áudio, com dispositivo).
    static void OnProcess(void* user, float* frames, ma_uint64 frameCount) {
        auto* self = static_cast<Impl*>(user);
        for (ma_uint32 c = 0; c < kChannels; ++c) {
            double sum = 0.0;
            for (ma_uint64 i = 0; i < frameCount; ++i) sum += double(frames[i * kChannels + c]) * frames[i * kChannels + c];
            self->level[c].store(frameCount ? float(std::sqrt(sum / double(frameCount))) : 0.0f, std::memory_order_relaxed);
        }
    }

    bool Init(bool useDevice) {
        ma_engine_config config = ma_engine_config_init();
        config.channels = kChannels;
        config.onProcess = &Impl::OnProcess;
        config.pProcessUserData = this;
        if (!useDevice) {
            config.noDevice = MA_TRUE;
            config.sampleRate = kSampleRateWithoutDevice;
        }
        return ma_engine_init(&config, &engine) == MA_SUCCESS;
    }

    ~Impl() {
        voices.clear();  // os sons antes do engine
        if (engineOk) ma_engine_uninit(&engine);
    }
};

AudioSystem::AudioSystem() : m_impl(std::make_unique<Impl>()) {}
AudioSystem::~AudioSystem() = default;

void AudioSystem::OnStart(SceneRuntime& runtime) {
    m_runtime = &runtime;
    m_paused = false;
    m_impl->settings = AudioSettings::Global();
    // As fontes com "Tocar ao iniciar" começam antes do OnStart dos scripts (que já podem pará-las).
    OnUpdate(runtime, 0.0f);
}

// O dispositivo só abre quando o primeiro som vai tocar: uma cena sem sons (e os testes) não abrem a placa.
bool AudioSystem::EnsureEngine() {
    Impl& impl = *m_impl;
    if (impl.initTried) return impl.engineOk;
    impl.initTried = true;
    if (impl.settings.useDevice && impl.Init(true)) {
        impl.engineOk = impl.hasDevice = true;
    } else {
        // Sem saída de som: o jogo segue igual, misturando num dispositivo nulo.
        if (impl.settings.useDevice) log::Warn("Áudio: nenhum dispositivo de saída disponível; o jogo segue sem som");
        impl.engineOk = impl.Init(false);
        if (!impl.engineOk) log::Error("Áudio: o miniaudio não iniciou; os sons ficam desligados neste Play");
    }
    if (impl.engineOk && m_runtime) UpdateListener(m_runtime->GetScene());
    return impl.engineOk;
}

void AudioSystem::OnStop(SceneRuntime&) {
    m_impl = std::make_unique<Impl>();  // para e libera todos os sons e o dispositivo
    m_runtime = nullptr;
    m_paused = false;
}

void AudioSystem::OnUpdate(SceneRuntime& runtime, float) {
    Scene& scene = runtime.GetScene();
    if (m_impl->engineOk) UpdateListener(scene);

    for (const auto& e : scene.Entities()) {
        const AudioSource* source = e->Get<AudioSource>();
        if (!source) continue;
        const EntityId id = e->id;
        auto it = m_impl->voices.find(id);
        if (!scene.IsActiveInHierarchy(id)) {
            // Desativada: para; ao reativar, "Tocar ao iniciar" vale de novo.
            if (it != m_impl->voices.end()) m_impl->voices.erase(it);
            m_impl->seen.erase(id);
            continue;
        }
        if (it != m_impl->voices.end() && it->second->file != source->clip.file) {
            m_impl->voices.erase(it);  // trocou o clipe: o som antigo para
            it = m_impl->voices.end();
        }
        if (m_impl->seen.insert(id).second && source->playOnStart) {
            Play(id);
            continue;
        }
        if (it != m_impl->voices.end()) ApplySource(*it->second, *source, scene, id);
    }
    // Sons de entidades excluídas ou que perderam o AudioSource.
    for (auto it = m_impl->voices.begin(); it != m_impl->voices.end();) {
        const Entity* e = scene.Find(it->first);
        if (!e || !e->Has<AudioSource>()) {
            m_impl->seen.erase(it->first);
            it = m_impl->voices.erase(it);
        } else {
            ++it;
        }
    }
}

void AudioSystem::UpdateListener(Scene& scene) {
    EntityId listener = kNullEntity;
    float volume = 1.0f;
    for (const auto& e : scene.Entities()) {
        const AudioListener* l = e->Get<AudioListener>();
        if (l && scene.IsActiveInHierarchy(e->id)) {
            listener = e->id;
            volume = std::max(0.0f, l->volume);
            break;
        }
    }
    if (!listener) listener = scene.FindMainCamera();

    glm::vec3 position{0.0f}, forward{0.0f, 0.0f, -1.0f}, up{0.0f, 1.0f, 0.0f};
    if (listener) {
        const glm::mat4 world = scene.WorldMatrix(listener);
        position = glm::vec3(world[3]);
        if (const glm::vec3 f = -glm::vec3(world[2]); glm::length(f) > 1e-6f) forward = glm::normalize(f);
        if (const glm::vec3 u = glm::vec3(world[1]); glm::length(u) > 1e-6f) up = glm::normalize(u);
    }
    m_listenerPosition = position;
    ma_engine_listener_set_position(&m_impl->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_impl->engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m_impl->engine, 0, up.x, up.y, up.z);
    ma_engine_set_volume(&m_impl->engine, volume * std::max(0.0f, m_impl->settings.masterVolume));
}

void AudioSystem::ApplySource(Voice& voice, const AudioSource& source, Scene& scene, EntityId id) {
    ma_sound* s = &voice.sound;
    ma_sound_set_volume(s, std::max(0.0f, source.volume));
    ma_sound_set_pitch(s, std::max(0.01f, source.pitch));
    ma_sound_set_looping(s, source.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_spatialization_enabled(s, source.spatial ? MA_TRUE : MA_FALSE);
    voice.spatial = source.spatial;
    voice.gain = std::max(0.0f, source.volume);
    voice.distance = 0.0f;
    if (source.spatial) {
        const float minDistance = std::max(0.01f, source.minDistance);
        const float maxDistance = std::max(minDistance, source.maxDistance);
        const glm::vec3 p = glm::vec3(scene.WorldMatrix(id)[3]);
        ma_sound_set_position(s, p.x, p.y, p.z);
        ma_sound_set_min_distance(s, minDistance);
        ma_sound_set_max_distance(s, maxDistance);
        voice.distance = glm::length(p - m_listenerPosition);
        voice.gain *= DistanceGain(voice.distance, minDistance, maxDistance);
    }
}

AudioSystem::Voice* AudioSystem::VoiceFor(EntityId id, const AudioSource& source) {
    if (auto it = m_impl->voices.find(id); it != m_impl->voices.end()) {
        if (it->second->file == source.clip.file) return it->second.get();
        m_impl->voices.erase(it);
    }
    const std::filesystem::path& file = source.clip.file;
    const std::string name = source.clip.path.empty() ? ToUtf8(file) : source.clip.path;
    if (file.empty()) {
        if (m_impl->failed.insert(std::filesystem::path()).second) {
            log::Warn("Áudio: fonte de som sem clipe ({})", source.clip.IsEmpty() ? "nenhum escolhido" : name);
        }
        return nullptr;
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    const ma_uint32 flags = (!ec && size > kStreamAbove) ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    auto voice = std::make_unique<Voice>();
#ifdef _WIN32
    const ma_result result = ma_sound_init_from_file_w(&m_impl->engine, file.c_str(), flags, nullptr, nullptr, &voice->sound);
#else
    const ma_result result = ma_sound_init_from_file(&m_impl->engine, file.c_str(), flags, nullptr, nullptr, &voice->sound);
#endif
    if (result != MA_SUCCESS) {
        if (m_impl->failed.insert(file).second) {
            log::Warn("Áudio: não foi possível abrir \"{}\" ({})", name, ma_result_description(result));
        }
        return nullptr;
    }
    voice->initialized = true;
    ma_sound_set_doppler_factor(&voice->sound, 0.0f);  // sem efeito Doppler: as entidades "teleportam"
    voice->file = file;
    voice->clip = name;
    return m_impl->voices.emplace(id, std::move(voice)).first->second.get();
}

Entity* AudioSystem::FindEntity(std::string_view name) const {
    return m_runtime ? m_runtime->GetScene().FindByName(name) : nullptr;
}

Entity* AudioSystem::FindEntity(EntityId id) const {
    return m_runtime ? m_runtime->GetScene().Find(id) : nullptr;
}

bool AudioSystem::Play(EntityId id) {
    Entity* e = FindEntity(id);
    const AudioSource* source = e ? e->Get<AudioSource>() : nullptr;
    if (!source || !EnsureEngine()) return false;
    m_impl->seen.insert(id);  // tocado por script: "Tocar ao iniciar" não repete
    Voice* voice = VoiceFor(id, *source);
    if (!voice) return false;
    ApplySource(*voice, *source, m_runtime->GetScene(), id);
    ma_sound_seek_to_pcm_frame(&voice->sound, 0);
    if (m_paused) {
        voice->resumeOnUnpause = true;
    } else {
        ma_sound_start(&voice->sound);
    }
    return true;
}

bool AudioSystem::Play(std::string_view entity) {
    const Entity* e = FindEntity(entity);
    return e && Play(e->id);
}

bool AudioSystem::Stop(EntityId id) {
    const Entity* e = FindEntity(id);
    if (!e || !e->Has<AudioSource>()) return false;
    if (auto it = m_impl->voices.find(id); it != m_impl->voices.end()) {
        ma_sound_stop(&it->second->sound);
        ma_sound_seek_to_pcm_frame(&it->second->sound, 0);
        it->second->resumeOnUnpause = false;
    }
    return true;
}

bool AudioSystem::Stop(std::string_view entity) {
    const Entity* e = FindEntity(entity);
    return e && Stop(e->id);
}

bool AudioSystem::SetVolume(EntityId id, float volume) {
    Entity* e = FindEntity(id);
    AudioSource* source = e ? e->Get<AudioSource>() : nullptr;
    if (!source) return false;
    source->volume = std::max(0.0f, volume);
    if (auto it = m_impl->voices.find(id); it != m_impl->voices.end()) {
        ApplySource(*it->second, *source, m_runtime->GetScene(), id);
    }
    return true;
}

bool AudioSystem::SetVolume(std::string_view entity, float volume) {
    const Entity* e = FindEntity(entity);
    return e && SetVolume(e->id, volume);
}

bool AudioSystem::IsPlaying(EntityId id) const {
    const auto it = m_impl->voices.find(id);
    if (it == m_impl->voices.end()) return false;
    return it->second->resumeOnUnpause || ma_sound_is_playing(&it->second->sound);
}

bool AudioSystem::IsPlaying(std::string_view entity) const {
    const Entity* e = FindEntity(entity);
    return e && IsPlaying(e->id);
}

void AudioSystem::StopAll() {
    for (auto& [id, voice] : m_impl->voices) {
        ma_sound_stop(&voice->sound);
        voice->resumeOnUnpause = false;
    }
}

void AudioSystem::SetPaused(bool paused) {
    if (paused == m_paused) return;
    m_paused = paused;
    for (auto& [id, voice] : m_impl->voices) {
        if (paused && ma_sound_is_playing(&voice->sound)) {
            ma_sound_stop(&voice->sound);  // guarda a posição: start continua dali
            voice->resumeOnUnpause = true;
        } else if (!paused && voice->resumeOnUnpause) {
            ma_sound_start(&voice->sound);
            voice->resumeOnUnpause = false;
        }
    }
}

bool AudioSystem::HasDevice() const { return m_impl->hasDevice; }

int AudioSystem::SampleRate() const {
    return m_impl->engineOk ? int(ma_engine_get_sample_rate(&m_impl->engine)) : 0;
}

std::vector<float> AudioSystem::ReadMix(int frames) {
    if (!m_impl->engineOk || m_impl->hasDevice || frames <= 0) return {};
    std::vector<float> out(size_t(frames) * kChannels, 0.0f);
    ma_uint64 read = 0;
    ma_engine_read_pcm_frames(&m_impl->engine, out.data(), ma_uint64(frames), &read);
    out.resize(size_t(read) * kChannels);
    return out;
}

float AudioSystem::OutputLevel(int channel) const {
    if (channel < 0 || channel >= int(kChannels)) return 0.0f;
    return m_impl->level[channel].load(std::memory_order_relaxed);
}

std::vector<AudioVoiceInfo> AudioSystem::Voices() const {
    std::vector<AudioVoiceInfo> out;
    for (const auto& [id, voice] : m_impl->voices) {
        out.push_back({id, voice->clip, IsPlaying(id), voice->spatial, voice->distance, voice->gain});
    }
    return out;
}

} // namespace motor
