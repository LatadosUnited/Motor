#pragma once

// Áudio do jogo (Fase 8): sistema do SceneRuntime (ordem kSystemOrderAudio, depois dos scripts) que toca as
// fontes de som (AudioSource) com o miniaudio. Só existe durante o Play: parar o Play para todos os sons.
//
// Num script:
//   if (auto* audio = GetSystem<motor::AudioSystem>()) {
//       if (pegouMoeda) audio->Play("SomMoeda");     // a entidade com o AudioSource
//       audio->SetVolume("Musica", 0.3f);
//   }
//
// A cada OnUpdate o sistema lê os componentes: volume, tom, repetir e posição (som 3D) valem na hora. O
// ouvinte é a entidade ativa com AudioListener (senão, a câmera principal). Sem dispositivo de áudio (testes,
// CI, computador sem saída de som) o sistema mistura num dispositivo "nulo": tudo funciona, só não se ouve.

#include "motor/scene/RuntimeSystem.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace motor {

class Scene;
struct Entity;
struct AudioSource;
using EntityId = uint32_t;

// Configuração global do áudio, lida a cada Play (cada AudioSystem copia ao iniciar).
struct AudioSettings {
    bool useDevice = true;      // false: não abre o dispositivo (testes); o som é misturado só em ReadMix
    float masterVolume = 1.0f;  // multiplica tudo (além do volume do AudioListener)

    static AudioSettings& Global();
};

// Estado de uma fonte que já tocou neste Play (diagnóstico: editor, servidor MCP, testes).
struct AudioVoiceInfo {
    EntityId entity = 0;
    std::string clip;        // caminho do clipe no projeto
    bool playing = false;
    bool spatial = false;
    float distance = 0.0f;   // até o ouvinte (3D)
    float gain = 1.0f;       // volume da fonte x queda com a distância (3D), sem o volume geral
};

class AudioSystem final : public RuntimeSystem {
public:
    AudioSystem();
    ~AudioSystem() override;

    void OnStart(SceneRuntime& runtime) override;
    void OnUpdate(SceneRuntime& runtime, float dt) override;
    void OnStop(SceneRuntime& runtime) override;

    // --- Scripts: pelo nome da entidade (a primeira com esse nome) ou pelo id ---
    // Toca o clipe do AudioSource desde o começo (se já estava tocando, recomeça). false se a entidade não
    // existe, não tem AudioSource, o clipe está vazio ou o arquivo não abriu (com aviso no log).
    bool Play(std::string_view entity);
    bool Play(EntityId entity);
    // Para o som da entidade. false se ela não tem AudioSource.
    bool Stop(std::string_view entity);
    bool Stop(EntityId entity);
    // Muda AudioSource::volume (0 = mudo, 1 = normal; vale na hora, mesmo tocando).
    bool SetVolume(std::string_view entity, float volume);
    bool SetVolume(EntityId entity, float volume);
    bool IsPlaying(std::string_view entity) const;
    bool IsPlaying(EntityId entity) const;
    void StopAll();

    // --- Quem roda o jogo ---
    // Pausa todos os sons (o Play do editor em pausa); ao continuar, cada um volta de onde estava.
    void SetPaused(bool paused);
    bool IsPaused() const { return m_paused; }
    // Tocando num dispositivo de verdade (false: sem saída de som, ou AudioSettings::useDevice desligado).
    bool HasDevice() const;
    // Mistura `frames` quadros estéreo (sem dispositivo) e devolve as amostras intercaladas (E, D, E, D...).
    // Com dispositivo, devolve vazio: quem mistura é a placa de som. Usado pelos testes.
    std::vector<float> ReadMix(int frames);
    int SampleRate() const;

    // --- Diagnóstico ---
    std::vector<AudioVoiceInfo> Voices() const;
    glm::vec3 ListenerPosition() const { return m_listenerPosition; }
    // Nível (RMS) da saída no último bloco misturado, por canal: o som que sai de fato.
    float OutputLevel(int channel) const;

private:
    struct Impl;
    struct Voice;

    Entity* FindEntity(std::string_view name) const;
    Entity* FindEntity(EntityId id) const;
    bool EnsureEngine();
    Voice* VoiceFor(EntityId id, const AudioSource& source);  // cria (abre o arquivo) se faltar
    void UpdateListener(Scene& scene);
    void ApplySource(Voice& voice, const AudioSource& source, Scene& scene, EntityId id);

    std::unique_ptr<Impl> m_impl;
    SceneRuntime* m_runtime = nullptr;
    glm::vec3 m_listenerPosition{0.0f};
    bool m_paused = false;
};

} // namespace motor
