#pragma once

// Fonte de som (Fase 8): toca um clipe (.wav/.ogg/.mp3 do projeto) durante o jogo. Só dados: quem toca é o
// AudioSystem (motor/audio/AudioSystem.h), que só existe no Play. Som 3D sai da posição da entidade e é
// ouvido pelo AudioListener (ou pela câmera principal); som 2D (música, cliques da UI) ignora a posição.

#include "motor/scene/Component.h"

#include <filesystem>
#include <string>
#include <vector>

namespace motor {

class Scene;

// Clipe de som: um arquivo de áudio do projeto, pelo GUID (com o caminho junto, como plano B).
struct AudioClip {
    std::string guid;
    std::string path;  // relativo ao projeto ("Assets/Sons/Passo.wav")
    // Arquivo no disco, resolvido pela AssetLibrary ao ler a cena (ou ao escolher no editor). Não vai para a
    // cena nem entra na comparação: é só onde o AudioSystem acha o arquivo durante o jogo.
    std::filesystem::path file;

    bool IsEmpty() const { return guid.empty() && path.empty(); }
    bool operator==(const AudioClip& other) const { return guid == other.guid && path == other.path; }
};

struct AudioSource {
    AudioClip clip;
    float volume = 1.0f;
    float pitch = 1.0f;          // 2 = uma oitava acima (e duas vezes mais rápido)
    bool loop = false;
    bool playOnStart = true;     // toca sozinho ao iniciar o Play (ou ao aparecer durante o jogo)
    bool spatial = true;         // som 3D: volume e lado mudam com a posição em relação ao ouvinte
    float minDistance = 1.0f;    // 3D: até esta distância, volume cheio
    float maxDistance = 30.0f;   // 3D: a partir desta, o volume para de cair
};

template <> struct FieldTraits<AudioClip> {
    static CustomFieldType Describe();
};

// Clipes usados pelas fontes de som da cena (entidades ativas ou não), sem repetir, na ordem da cena. Para
// quem precisa saber quais arquivos de som uma cena usa (exportar o jogo).
std::vector<AudioClip> CollectAudioClips(const Scene& scene);

} // namespace motor
