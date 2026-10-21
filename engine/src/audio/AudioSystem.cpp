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
#include <future>
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
constexpr size_t kMaxOneShots = 8;  // vozes extras por fonte (PlayOneShot): a mais antiga para

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
    AudioGroup group = AudioGroup::Effects;
    float scale = 1.0f;  // PlayOneShot: volume em relação ao da fonte

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
    std::map<EntityId, std::vector<std::unique_ptr<Voice>>> shots;  // PlayOneShot, por fonte
    ma_sound_group groups[kAudioGroupCount]{};
    bool groupsOk = false;
    std::set<std::filesystem::path> preloaded;  // clipes já vistos pela pré-carga
    std::map<std::filesystem::path, std::shared_future<void>> loading;  // pronto quando decodificado
    std::vector<std::future<void>> workers;  // threads da pré-carga
    size_t preloadRequests = 0;  // pedidos ao carregador (decodificados fora do jogo)
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
        for (auto& w : workers) w.wait();  // a pré-carga usa o carregador do engine
        shots.clear();  // os sons antes dos grupos e do engine
        voices.clear();
        if (groupsOk) {
            for (ma_sound_group& g : groups) ma_sound_group_uninit(&g);
        }
        if (engineOk) ma_engine_uninit(&engine);
    }

    ma_sound_group* Group(AudioGroup group) {
        return groupsOk ? &groups[static_cast<int>(group)] : nullptr;
    }
};

AudioSystem::AudioSystem() : m_impl(std::make_unique<Impl>()) {}
AudioSystem::~AudioSystem() = default;

void AudioSystem::OnStart(SceneRuntime& runtime) {
    m_runtime = &runtime;
    m_paused = false;
    m_impl->settings = AudioSettings::Global();
    Preload(runtime.GetScene());  // a decodificação começa já, numa thread do miniaudio
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
    if (impl.engineOk) {
        impl.groupsOk = true;
        for (int i = 0; i < kAudioGroupCount; ++i) {
            if (ma_sound_group_init(&impl.engine, 0, nullptr, &impl.groups[i]) != MA_SUCCESS) {
                for (int j = 0; j < i; ++j) ma_sound_group_uninit(&impl.groups[j]);
                impl.groupsOk = false;
                log::Warn("Áudio: grupos de volume indisponíveis neste Play");
                break;
            }
            const float volume = impl.settings.groupVolumes[static_cast<size_t>(i)];
            ma_sound_group_set_volume(&impl.groups[i], std::max(0.0f, volume));
        }
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
    Preload(scene);  // fontes que apareceram durante o jogo (prefabs instanciados)
    if (m_impl->engineOk) UpdateListener(scene);

    for (EntityId id : scene.EntitiesWith<AudioSource>()) {
        const Entity* e = scene.Find(id);
        const AudioSource* source = e ? e->Get<AudioSource>() : nullptr;
        if (!source) continue;
        auto it = m_impl->voices.find(id);
        if (!scene.IsActiveInHierarchy(id)) {
            // Desativada: para; ao reativar, "Tocar ao iniciar" vale de novo.
            if (it != m_impl->voices.end()) m_impl->voices.erase(it);
            m_impl->shots.erase(id);
            m_impl->seen.erase(id);
            continue;
        }
        if (it != m_impl->voices.end() &&
            (it->second->file != source->clip.file || it->second->group != source->group)) {
            m_impl->voices.erase(it);  // trocou o clipe ou o grupo: o som antigo para
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
    UpdateOneShots(scene);
}

void AudioSystem::UpdateListener(Scene& scene) {
    EntityId listener = kNullEntity;
    float volume = 1.0f;
    for (EntityId id : scene.EntitiesWith<AudioListener>()) {
        const Entity* e = scene.Find(id);
        const AudioListener* l = e ? e->Get<AudioListener>() : nullptr;
        if (l && scene.IsActiveInHierarchy(id)) {
            listener = id;
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
        if (it->second->file == source.clip.file && it->second->group == source.group) return it->second.get();
        m_impl->voices.erase(it);
    }
    std::unique_ptr<Voice> voice = OpenVoice(source.clip, source.group);
    if (!voice) return nullptr;
    return m_impl->voices.emplace(id, std::move(voice)).first->second.get();
}

std::unique_ptr<AudioSystem::Voice> AudioSystem::OpenVoice(const AudioClip& clip, AudioGroup group) {
    const std::filesystem::path& file = clip.file;
    const std::string name = clip.path.empty() ? ToUtf8(file) : clip.path;
    if (file.empty()) {
        if (m_impl->failed.insert(std::filesystem::path()).second) {
            log::Warn("Áudio: fonte de som sem clipe ({})", clip.IsEmpty() ? "nenhum escolhido" : name);
        }
        return nullptr;
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    const ma_uint32 flags = (!ec && size > kStreamAbove) ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    if (const auto it = m_impl->loading.find(file); it != m_impl->loading.end()) it->second.wait();
    auto voice = std::make_unique<Voice>();
    ma_sound_group* parent = m_impl->Group(group);
#ifdef _WIN32
    const ma_result result = ma_sound_init_from_file_w(&m_impl->engine, file.c_str(), flags, parent, nullptr, &voice->sound);
#else
    const ma_result result = ma_sound_init_from_file(&m_impl->engine, file.c_str(), flags, parent, nullptr, &voice->sound);
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
    voice->group = group;
    return voice;
}

void AudioSystem::Preload(Scene& scene) {
    std::vector<std::filesystem::path> batch;
    for (EntityId id : scene.EntitiesWith<AudioSource>()) {
        const Entity* e = scene.Find(id);
        const AudioSource* source = e ? e->Get<AudioSource>() : nullptr;
        if (!source || source->clip.file.empty() || m_impl->preloaded.contains(source->clip.file)) continue;
        const std::filesystem::path& file = source->clip.file;
        m_impl->preloaded.insert(file);
        std::error_code ec;
        const auto size = std::filesystem::file_size(file, ec);
        if (ec || size > kStreamAbove) continue;  // sem arquivo, ou música (lida aos poucos durante o jogo)
        batch.push_back(file);
    }
    if (batch.empty() || !EnsureEngine()) return;
    // Uma thread decodifica os clipes, um por vez, no carregador do miniaudio (que guarda o resultado pelo
    // caminho): o primeiro Play de cada um já acha o som pronto. Quem tocar antes espera só o seu clipe.
    std::vector<std::promise<void>> done(batch.size());
    for (size_t i = 0; i < batch.size(); ++i) m_impl->loading[batch[i]] = done[i].get_future().share();
    m_impl->preloadRequests += batch.size();
    ma_resource_manager* loader = ma_engine_get_resource_manager(&m_impl->engine);
    m_impl->workers.push_back(std::async(std::launch::async, [loader, batch, done = std::move(done)]() mutable {
        for (size_t i = 0; i < batch.size(); ++i) {
#ifdef _WIN32
            ma_resource_manager_register_file_w(loader, batch[i].c_str(), MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE);
#else
            ma_resource_manager_register_file(loader, batch[i].c_str(), MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE);
#endif
            done[i].set_value();
        }
    }));
}

void AudioSystem::UpdateOneShots(Scene& scene) {
    for (auto it = m_impl->shots.begin(); it != m_impl->shots.end();) {
        const EntityId id = it->first;
        const Entity* e = scene.Find(id);
        const AudioSource* source = e ? e->Get<AudioSource>() : nullptr;
        if (!source || !scene.IsActiveInHierarchy(id)) {
            it = m_impl->shots.erase(it);
            continue;
        }
        auto& list = it->second;
        std::erase_if(list, [](const std::unique_ptr<Voice>& v) {
            return !v->resumeOnUnpause && !ma_sound_is_playing(&v->sound);  // terminou (ou foi parada)
        });
        for (auto& voice : list) ApplyOneShot(*voice, *source, scene, id);
        it = list.empty() ? m_impl->shots.erase(it) : std::next(it);
    }
}

void AudioSystem::ApplyOneShot(Voice& voice, const AudioSource& source, Scene& scene, EntityId id) {
    ApplySource(voice, source, scene, id);
    ma_sound_set_looping(&voice.sound, MA_FALSE);  // voz extra nunca repete
    ma_sound_set_volume(&voice.sound, std::max(0.0f, source.volume) * voice.scale);
    voice.gain *= voice.scale;
}

bool AudioSystem::PlayOneShot(EntityId id, const AudioClip* clip, float volumeScale) {
    Entity* e = FindEntity(id);
    const AudioSource* source = e ? e->Get<AudioSource>() : nullptr;
    if (!source || !EnsureEngine()) return false;
    std::unique_ptr<Voice> voice = OpenVoice(clip ? *clip : source->clip, source->group);
    if (!voice) return false;
    voice->scale = std::max(0.0f, volumeScale);
    auto& list = m_impl->shots[id];
    if (list.size() >= kMaxOneShots) list.erase(list.begin());  // a mais antiga para
    Voice& v = *list.emplace_back(std::move(voice));
    ApplyOneShot(v, *source, m_runtime->GetScene(), id);
    if (m_paused) {
        v.resumeOnUnpause = true;
    } else {
        ma_sound_start(&v.sound);
    }
    return true;
}

bool AudioSystem::PlayOneShot(EntityId entity, float volumeScale) {
    return PlayOneShot(entity, nullptr, volumeScale);
}

bool AudioSystem::PlayOneShot(EntityId entity, const AudioClip& clip, float volumeScale) {
    return PlayOneShot(entity, &clip, volumeScale);
}

bool AudioSystem::PlayOneShot(std::string_view entity, float volumeScale) {
    const Entity* e = FindEntity(entity);
    return e && PlayOneShot(e->id, nullptr, volumeScale);
}

void AudioSystem::SetGroupVolume(AudioGroup group, float volume) {
    const auto i = static_cast<size_t>(group);
    volume = std::max(0.0f, volume);
    AudioSettings::Global().groupVolumes[i] = volume;  // próximas cenas e próximo Play
    m_impl->settings.groupVolumes[i] = volume;
    if (ma_sound_group* g = m_impl->Group(group)) ma_sound_group_set_volume(g, volume);
}

float AudioSystem::GroupVolume(AudioGroup group) const {
    return m_impl->settings.groupVolumes[static_cast<size_t>(group)];
}

size_t AudioSystem::PreloadedClips() const {
    return m_impl->preloadRequests;
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
    m_impl->shots.erase(id);  // as vozes extras também param
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
    auto playing = [](const Voice& v) { return v.resumeOnUnpause || ma_sound_is_playing(&v.sound); };
    if (const auto it = m_impl->voices.find(id); it != m_impl->voices.end() && playing(*it->second)) return true;
    const auto shots = m_impl->shots.find(id);
    return shots != m_impl->shots.end() &&
           std::any_of(shots->second.begin(), shots->second.end(), [&](const auto& v) { return playing(*v); });
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
    m_impl->shots.clear();
}

void AudioSystem::SetPaused(bool paused) {
    if (paused == m_paused) return;
    m_paused = paused;
    auto apply = [paused](Voice& voice) {
        if (paused && ma_sound_is_playing(&voice.sound)) {
            ma_sound_stop(&voice.sound);  // guarda a posição: start continua dali
            voice.resumeOnUnpause = true;
        } else if (!paused && voice.resumeOnUnpause) {
            ma_sound_start(&voice.sound);
            voice.resumeOnUnpause = false;
        }
    };
    for (auto& [id, voice] : m_impl->voices) apply(*voice);
    for (auto& [id, list] : m_impl->shots) {
        for (auto& voice : list) apply(*voice);
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
    // Vozes extras: somadas na fonte (ou numa linha própria, se ela ainda não tocou a voz principal).
    for (const auto& [id, list] : m_impl->shots) {
        auto it = std::find_if(out.begin(), out.end(), [id](const AudioVoiceInfo& v) { return v.entity == id; });
        if (it == out.end() && !list.empty()) {
            const Voice& first = *list.front();
            it = out.insert(out.end(), {id, first.clip, true, first.spatial, first.distance, first.gain});
        }
        if (it != out.end()) it->oneShots = static_cast<int>(list.size());
    }
    return out;
}

} // namespace motor
