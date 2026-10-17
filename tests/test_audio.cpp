// Áudio (Fase 8), sem dispositivo de som: o AudioSystem mistura num dispositivo nulo e o teste lê a mistura.
// - Arquivos .wav/.ogg/.mp3 como assets com GUID; clipe na cena pelo GUID (arquivo movido continua achado).
// - Som 3D: volume cai com a distância do ouvinte e o lado (esquerda/direita) segue a direção da câmera.
// - Som 2D (clique da UI) tocado por script, volume, parar, pausa, fonte desativada/excluída, fim do Play.
// - Arquivo inválido ou clipe vazio: Play devolve false e o jogo segue.
#include "TestUtil.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/assets/AssetLoaders.h>
#include <motor/audio/AudioSystem.h>
#include <motor/scene/Components.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/scene/SceneSerializer.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

using namespace motor;
namespace fs = std::filesystem;

namespace {

// WAV PCM 16 bits mono com um seno de `hz` (amplitude 0.5).
void WriteSineWav(const fs::path& file, float seconds, float hz = 440.0f, int rate = 48000) {
    const uint32_t frames = uint32_t(seconds * float(rate));
    std::vector<int16_t> samples(frames);
    for (uint32_t i = 0; i < frames; ++i) {
        samples[i] = int16_t(16000.0 * std::sin(2.0 * std::numbers::pi * hz * double(i) / rate));
    }
    std::ofstream out(file, std::ios::binary);
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t dataBytes = frames * 2;
    out.write("RIFF", 4);
    u32(36 + dataBytes);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);  // PCM
    u16(1);  // mono
    u32(uint32_t(rate));
    u32(uint32_t(rate) * 2);
    u16(2);
    u16(16);
    out.write("data", 4);
    u32(dataBytes);
    out.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
}

struct Level {
    float left = 0.0f;
    float right = 0.0f;
    float Mean() const { return (left + right) * 0.5f; }
};

// Nível (RMS) de cada canal em `frames` quadros da mistura. Antes, descarta um bloco: o miniaudio suaviza as
// mudanças de volume e de posição em alguns milissegundos.
Level Measure(AudioSystem& audio, int frames = 4800) {
    audio.ReadMix(2400);
    const std::vector<float> mix = audio.ReadMix(frames);
    Level level;
    const size_t n = mix.size() / 2;
    if (n == 0) return level;
    double l = 0.0, r = 0.0;
    for (size_t i = 0; i < n; ++i) {
        l += double(mix[i * 2]) * mix[i * 2];
        r += double(mix[i * 2 + 1]) * mix[i * 2 + 1];
    }
    level.left = float(std::sqrt(l / double(n)));
    level.right = float(std::sqrt(r / double(n)));
    return level;
}

AudioClip ClipFor(AssetLibrary& assets, const fs::path& file) {
    AudioClip clip;
    clip.guid = assets.Database().GuidFor(file);
    clip.path = assets.KeyForFile(file);
    clip.file = file;
    return clip;
}

} // namespace

int main(int argc, char** argv) {
    const fs::path dir = TestDir(argc, argv, "test_audio_tmp");
    const fs::path project = dir / "Jogo Ação";
    const fs::path sounds = project / "Assets" / "Sons";
    fs::create_directories(sounds);
    const fs::path loopFile = sounds / "Motor ligado.wav";  // som 3D em loop
    const fs::path clickFile = sounds / "Clique ção.wav";   // som curto da UI
    const fs::path brokenFile = sounds / "Quebrado.wav";
    WriteSineWav(loopFile, 1.0f);
    WriteSineWav(clickFile, 0.2f, 880.0f);
    std::ofstream(brokenFile, std::ios::binary) << "isto não é um wav";

    AssetLibrary assets;
    assets.SetRoot(project);
    assets.Database().SetAssetsDir(project / "Assets");
    AudioSettings::Global().useDevice = false;  // nunca abre a placa de som nos testes

    SECTION("Arquivos de som são assets com GUID");
    {
        for (const char* name : {"a.wav", "b.OGG", "c.mp3"}) CHECK(AssetTypeFromPath(name) == AssetType::Audio);
        CHECK(std::string(AssetTypeName(AssetType::Audio)) == "Som");
        CHECK(AssetDatabase::IsTrackedAsset(loopFile));
        const std::string guid = assets.Database().GuidFor(loopFile);
        CHECK(AssetDatabase::IsValidGuid(guid) && fs::exists(AssetDatabase::MetaPathFor(loopFile)));
        const auto keys = assets.ScanFiles(AssetType::Audio);
        CHECK(keys.size() == 3);
    }

    SECTION("Clipe na cena pelo GUID; arquivo movido com o .meta continua achado");
    {
        Scene scene;
        Entity& e = scene.CreateEntity("Motor");
        AudioSource& source = e.Add<AudioSource>();
        source.clip = ClipFor(assets, loopFile);
        source.volume = 0.5f;
        source.loop = true;
        source.spatial = false;
        source.maxDistance = 12.0f;
        scene.CreateEntity("Outro").Add<AudioSource>().clip = source.clip;  // mesmo clipe: lista sem repetir
        scene.CreateEntity("Vazio").Add<AudioSource>();
        e.Add<AudioListener>().volume = 0.8f;

        const std::string json = SceneSerializer::ToJson(scene, assets);
        CHECK(json.find(source.clip.guid) != std::string::npos);
        CHECK(json.find("Assets/Sons/Motor ligado.wav") != std::string::npos);
        Scene loaded;
        std::string error;
        CHECK(SceneSerializer::FromJson(json, loaded, assets, &error));
        CHECK(loaded == scene);
        const AudioSource* back = loaded.FindByName("Motor")->Get<AudioSource>();
        CHECK(back && back->volume == 0.5f && back->loop && !back->spatial && back->maxDistance == 12.0f);
        CHECK(back && fs::equivalent(back->clip.file, loopFile));
        CHECK(loaded.FindByName("Motor")->Get<AudioListener>()->volume == 0.8f);
        CHECK(loaded.FindByName("Vazio")->Get<AudioSource>()->clip.IsEmpty());
        const auto clips = CollectAudioClips(loaded);
        CHECK(clips.size() == 1 && clips[0].guid == source.clip.guid);

        // O arquivo resolvido não entra na comparação (não é dado da cena).
        AudioClip other = source.clip;
        other.file = "outro lugar.wav";
        CHECK(other == source.clip);

        // Move o arquivo (com o .meta, como o painel Projeto faz): o GUID acha o lugar novo.
        const fs::path moved = project / "Assets" / "Música" / "Motor ligado.wav";
        fs::create_directories(moved.parent_path());
        fs::rename(loopFile, moved);
        fs::rename(AssetDatabase::MetaPathFor(loopFile), AssetDatabase::MetaPathFor(moved));
        assets.Database().Refresh();
        Scene again;
        CHECK(SceneSerializer::FromJson(json, again, assets, &error));
        const AudioSource* m = again.FindByName("Motor")->Get<AudioSource>();
        CHECK(m && m->clip.path == "Assets/Música/Motor ligado.wav" && fs::equivalent(m->clip.file, moved));
        fs::rename(moved, loopFile);
        fs::rename(AssetDatabase::MetaPathFor(moved), AssetDatabase::MetaPathFor(loopFile));
        assets.Database().Refresh();

        // Só o caminho (o que um agente manda pelo MCP): o GUID vem do .meta e o arquivo é achado.
        const CustomFieldType clipType = FieldTraits<AudioClip>::Describe();
        AudioClip byPath;
        clipType.fromJson(R"("Assets/Sons/Motor ligado.wav")", &byPath, assets);
        CHECK(byPath.guid == source.clip.guid && byPath.path == "Assets/Sons/Motor ligado.wav");
        CHECK(!byPath.file.empty() && fs::equivalent(byPath.file, loopFile));
        AudioClip missing;
        clipType.fromJson(R"("Assets/Sons/Não existe.wav")", &missing, assets);
        CHECK(missing.guid.empty() && missing.path == "Assets/Sons/Não existe.wav");
    }

    SECTION("Som 3D: cai com a distância e muda de lado com a câmera");
    {
        Scene scene;
        Entity& camera = scene.CreateEntity("Câmera");
        camera.Add<CameraComponent>();
        camera.Add<AudioListener>();
        const EntityId cameraId = camera.id;
        Entity& motor = scene.CreateEntity("Motor");
        motor.transform.position = {3.0f, 0.0f, 0.0f};  // à direita da câmera (que olha para -Z)
        AudioSource& source = motor.Add<AudioSource>();
        source.clip = ClipFor(assets, loopFile);
        source.loop = true;
        source.minDistance = 1.0f;
        source.maxDistance = 100.0f;

        CHECK(scene.Find(cameraId) != nullptr);
        SceneRuntime runtime(scene);
        runtime.Start();
        AudioSystem* audio = runtime.GetSystem<AudioSystem>();
        CHECK(audio != nullptr);
        if (!audio) return TestResult();
        CHECK(!audio->HasDevice() && audio->SampleRate() == 48000);
        CHECK(audio->IsPlaying("Motor"));  // "Tocar ao iniciar"

        const Level near = Measure(*audio);
        CHECK(near.Mean() > 0.01f);
        CHECK(near.right > near.left * 1.5f);  // fonte à direita

        // A câmera vai para longe: o som cai (modelo inverso, 3 m -> 30 m: ~10x mais baixo).
        scene.Find(cameraId)->transform.position = {-27.0f, 0.0f, 0.0f};
        runtime.Update(1.0f / 60.0f);
        const Level far = Measure(*audio);
        CHECK(far.Mean() > 0.0f && far.Mean() < near.Mean() * 0.25f);
        const auto voices = audio->Voices();
        CHECK(voices.size() == 1 && std::abs(voices[0].distance - 30.0f) < 0.01f && std::abs(voices[0].gain - 1.0f / 30.0f) < 0.001f);
        CHECK(audio->ListenerPosition() == glm::vec3(-27.0f, 0.0f, 0.0f));

        // Volta e vira para trás (180° em Y): a fonte passa para a esquerda.
        scene.Find(cameraId)->transform.position = {0.0f, 0.0f, 0.0f};
        scene.Find(cameraId)->transform.rotation = glm::angleAxis(float(std::numbers::pi), glm::vec3(0, 1, 0));
        runtime.Update(1.0f / 60.0f);
        const Level turned = Measure(*audio);
        CHECK(turned.left > turned.right * 1.5f);
        CHECK(audio->OutputLevel(0) > 0.0f);

        // Sem AudioListener: o ouvinte é a câmera principal.
        scene.Find(cameraId)->Remove<AudioListener>();
        scene.Find(cameraId)->transform.position = {0.0f, 0.0f, 50.0f};
        runtime.Update(1.0f / 60.0f);
        CHECK(audio->ListenerPosition() == glm::vec3(0.0f, 0.0f, 50.0f));

        // Pausa (Play do editor em pausa): silêncio, mas o som continua "tocando" e volta ao continuar.
        audio->SetPaused(true);
        CHECK(audio->IsPaused() && audio->IsPlaying("Motor"));
        CHECK(Measure(*audio).Mean() < 1e-4f);
        audio->SetPaused(false);
        CHECK(Measure(*audio).Mean() > 1e-4f);

        // Fonte desativada: para; reativada: "Tocar ao iniciar" vale de novo.
        scene.Find(motor.id)->active = false;
        runtime.Update(1.0f / 60.0f);
        CHECK(!audio->IsPlaying("Motor") && Measure(*audio).Mean() < 1e-4f);
        scene.Find(motor.id)->active = true;
        runtime.Update(1.0f / 60.0f);
        CHECK(audio->IsPlaying("Motor"));

        // Fim do Play: o sistema e os sons somem.
        runtime.Stop();
        CHECK(runtime.GetSystem<AudioSystem>() == nullptr);
    }

    SECTION("Som 2D da UI tocado por script: Play, fim do clipe, volume, Stop e entidade excluída");
    {
        Scene scene;
        Entity& click = scene.CreateEntity("SomClique");
        click.transform.position = {500.0f, 0.0f, 0.0f};  // 2D: a posição não importa
        AudioSource& source = click.Add<AudioSource>();
        source.clip = ClipFor(assets, clickFile);
        source.spatial = false;
        source.playOnStart = false;
        Entity& broken = scene.CreateEntity("Quebrado");
        broken.Add<AudioSource>().clip = ClipFor(assets, brokenFile);
        broken.Get<AudioSource>()->playOnStart = false;
        scene.CreateEntity("SemClipe").Add<AudioSource>().playOnStart = false;
        scene.CreateEntity("SemFonte");

        SceneRuntime runtime(scene);
        runtime.Start();
        AudioSystem* audio = runtime.GetSystem<AudioSystem>();
        CHECK(audio != nullptr);
        if (!audio) return TestResult();
        CHECK(!audio->IsPlaying("SomClique") && Measure(*audio).Mean() < 1e-4f);

        CHECK(audio->Play("SomClique"));
        CHECK(audio->IsPlaying("SomClique"));
        const Level clickLevel = Measure(*audio, 2400);
        CHECK(clickLevel.Mean() > 0.05f && std::abs(clickLevel.left - clickLevel.right) < 0.01f);  // centro
        audio->ReadMix(48000);  // o clipe tem 0,2 s: acabou
        CHECK(!audio->IsPlaying("SomClique"));

        CHECK(audio->SetVolume("SomClique", 0.0f) && scene.FindByName("SomClique")->Get<AudioSource>()->volume == 0.0f);
        CHECK(audio->Play("SomClique") && Measure(*audio, 2400).Mean() < 1e-4f);
        CHECK(audio->SetVolume("SomClique", 1.0f));
        CHECK(Measure(*audio, 1200).Mean() > 0.05f);
        CHECK(audio->Stop("SomClique") && !audio->IsPlaying("SomClique"));

        CHECK(!audio->Play("Quebrado"));   // arquivo inválido: aviso no log, sem som
        CHECK(!audio->Play("SemClipe"));
        CHECK(!audio->Play("SemFonte") && !audio->Stop("SemFonte") && !audio->SetVolume("SemFonte", 1.0f));
        CHECK(!audio->Play("NãoExiste"));

        CHECK(audio->Play(scene.FindByName("SomClique")->id));
        scene.DestroyEntity(scene.FindByName("SomClique")->id);
        runtime.Update(1.0f / 60.0f);
        CHECK(audio->Voices().empty() && Measure(*audio).Mean() < 1e-4f);
        runtime.Stop();
    }

    SECTION("Fora do Play não há sistema de áudio");
    {
        Scene scene;
        SceneRuntime runtime(scene);
        CHECK(runtime.GetSystem<AudioSystem>() == nullptr);
    }

    return TestResult();
}
