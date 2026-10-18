// Desenhador do Inspector para o clipe do AudioSource (motor::AudioClip, ver motor/scene/AudioSource.h): combo
// com os .wav/.ogg/.mp3 do projeto, que também aceita o arquivo arrastado do painel Projeto.
#include "drawers/FieldDrawers.h"

#include <motor/scene/AudioSource.h>

#include <imgui.h>

MOTOR_FIELD_DRAWER(AudioClip) {
    auto& clip = *static_cast<motor::AudioClip*>(value);
    // Arquivo movido no painel Projeto (o .meta vai junto): acompanha o GUID. O caminho na cena é atualizado
    // ao salvar e abrir de novo; aqui só o arquivo que o jogo vai abrir (fora da comparação, sem desfazer).
    if (!clip.guid.empty()) {
        if (const auto file = ctx.assets.Database().PathFor(clip.guid); !file.empty() && file != clip.file) clip.file = file;
    }
    const std::string current = clip.path.empty() ? clip.guid : clip.path;
    const auto chosen = editor::AssetCombo(ctx, "##v", motor::AssetType::Audio, clip.IsEmpty() ? nullptr : &current,
                                           "(nenhum)");
    if (!clip.IsEmpty() && clip.file.empty()) ImGui::SetItemTooltip("Arquivo não encontrado: %s", current.c_str());
    if (!chosen) return;
    if (chosen->empty()) {
        clip = {};
        return;
    }
    clip.file = ctx.assets.FileForKey(*chosen);
    clip.guid = ctx.assets.Database().GuidFor(clip.file);
    clip.path = *chosen;
}
