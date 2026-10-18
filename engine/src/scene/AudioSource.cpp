#include "motor/scene/AudioSource.h"

#include "motor/assets/AssetLibrary.h"
#include "motor/scene/Scene.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace motor {

// Na cena: {"guid": "...", "path": "Assets/Sons/x.wav"}, ou null sem clipe. Ao ler, o GUID manda: o caminho
// é atualizado se o arquivo mudou de lugar (com o .meta). GUID desconhecido: vale o caminho.
CustomFieldType FieldTraits<AudioClip>::Describe() {
    return {"AudioClip",
            [](const void* a, const void* b) { return *static_cast<const AudioClip*>(a) == *static_cast<const AudioClip*>(b); },
            [](const void* v, const AssetLibrary&) {
                const auto& clip = *static_cast<const AudioClip*>(v);
                if (clip.IsEmpty()) return std::string("null");
                return nlohmann::ordered_json{{"guid", clip.guid}, {"path", clip.path}}.dump();
            },
            [](std::string_view text, void* v, AssetLibrary& assets, std::string* error) {
                const auto json = nlohmann::json::parse(text, nullptr, false);
                auto& clip = *static_cast<AudioClip*>(v);
                if (json.is_null()) {
                    clip = {};
                    return true;
                }
                if (json.is_string()) {
                    // Só o caminho ("Assets/Sons/x.wav"), como escreve um agente pelo MCP: o GUID vem do .meta.
                    clip = {};
                    clip.path = json.get<std::string>();
                    clip.file = assets.FileForKey(clip.path);
                    std::error_code ec;
                    if (!clip.file.empty() && std::filesystem::is_regular_file(clip.file, ec)) {
                        clip.guid = assets.Database().GuidFor(clip.file);
                    }
                    return true;
                }
                if (!json.is_object() || (!json.contains("guid") && !json.contains("path"))) {
                    if (error) *error = "esperado caminho \"Assets/...\", objeto {\"path\": \"Assets/...\"} ou null";
                    return false;
                }
                clip.guid = json.value("guid", std::string());
                clip.path = json.value("path", std::string());
                clip.file.clear();
                if (!clip.guid.empty()) clip.file = assets.Database().PathFor(clip.guid);
                if (!clip.file.empty()) {
                    if (const std::string key = assets.KeyForFile(clip.file); !key.empty()) clip.path = key;
                } else if (!clip.path.empty()) {
                    clip.file = assets.FileForKey(clip.path);
                }
                return true;
            }};
}

std::vector<AudioClip> CollectAudioClips(const Scene& scene) {
    std::vector<AudioClip> clips;
    for (const auto& e : scene.Entities()) {
        const AudioSource* source = e->Get<AudioSource>();
        if (!source || source->clip.IsEmpty()) continue;
        if (std::find(clips.begin(), clips.end(), source->clip) == clips.end()) clips.push_back(source->clip);
    }
    return clips;
}

// Os módulos rodam em ordem alfabética do nome (é a ordem do menu "Adicionar componente"): "Sound..." deixa
// os de áudio depois da Câmera, que abre a lista.
MOTOR_COMPONENT_MODULE(SoundSource) {
    registry.Register<AudioSource>("AudioSource", "Fonte de áudio")
        .Field("clip", "Clipe", &AudioSource::clip)
        .Field("volume", "Volume", &AudioSource::volume, {.slider = true, .min = 0.0f, .max = 1.0f})
        .Field("pitch", "Tom", &AudioSource::pitch,
               {.speed = 0.01f, .min = 0.1f, .max = 4.0f, .format = "%.2f", .tooltip = "2 = uma oitava acima (e mais rápido)"})
        .Field("loop", "Repetir", &AudioSource::loop)
        .Field("playOnStart", "Tocar ao iniciar", &AudioSource::playOnStart,
               {.tooltip = "Toca sozinho no começo do Play (desligado: só quando um script chamar Play)"})
        .Field("spatial", "Som 3D", &AudioSource::spatial,
               {.tooltip = "Volume e lado mudam com a posição em relação ao ouvinte (desligado: música, sons da UI)"})
        .Field("minDistance", "Distância mínima", &AudioSource::minDistance,
               {.speed = 0.1f, .min = 0.01f, .max = 10000.0f, .format = "%.1f m", .tooltip = "3D: até aqui, volume cheio"})
        .Field("maxDistance", "Distância máxima", &AudioSource::maxDistance,
               {.speed = 0.5f, .min = 0.01f, .max = 10000.0f, .format = "%.1f m", .tooltip = "3D: daqui em diante o volume para de cair"});
}

} // namespace motor
