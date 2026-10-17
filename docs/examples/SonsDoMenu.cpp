// Guia de scripts (docs/scripts.md), seção "Som": o som de clique de um botão da UI e a música que liga e
// desliga com a tecla M. Ponha numa entidade vazia; os sons são entidades com Fonte de áudio: "SomClique"
// (Som 3D e Tocar ao iniciar desligados) e "Musica" (Repetir ligado).

#include <motor/audio/AudioSystem.h>
#include <motor/scene/ScriptModule.h>
#include <motor/ui/UiSystem.h>

#include <string>

struct SonsDoMenu : motor::Script {
    std::string botao = "Jogar";          // botão da UI que faz o clique
    std::string somDoClique = "SomClique";
    std::string musica = "Musica";
    float volumeDaMusica = 0.5f;

    void OnStart() override {
        if (auto* audio = GetSystem<motor::AudioSystem>()) audio->SetVolume(musica, volumeDaMusica);
    }

    void OnUpdate(float) override {
        auto* audio = GetSystem<motor::AudioSystem>();
        if (!audio) return;
        if (auto* ui = GetSystem<motor::UiSystem>(); ui && ui->WasClicked(botao)) {
            audio->Play(somDoClique);  // recomeça do início se ainda estava tocando
        }
        if (WasKeyPressed(motor::Key::M)) {
            if (audio->IsPlaying(musica)) {
                audio->Stop(musica);
            } else {
                audio->Play(musica);
            }
        }
    }
};

MOTOR_SCRIPT(SonsDoMenu) {
    registry.Register<SonsDoMenu>("SonsDoMenu", "Sons do menu")
        .Field("botao", "Botão", &SonsDoMenu::botao)
        .Field("somDoClique", "Som do clique", &SonsDoMenu::somDoClique)
        .Field("musica", "Música", &SonsDoMenu::musica)
        .Field("volumeDaMusica", "Volume da música", &SonsDoMenu::volumeDaMusica,
               {.slider = true, .min = 0.0f, .max = 1.0f});
}
