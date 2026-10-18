#include "PlayMode.h"

#include <motor/audio/AudioSystem.h>
#include <motor/core/Log.h>

#include <utility>

namespace editor {

void PlayMode::Play(motor::Scene& scene, motor::EntityId selected) {
    if (IsActive()) return;
    m_backup = scene;  // cópia profunda: os scripts copiados começam sem dono e não iniciados
    m_backupSelected = selected;
    m_paused = false;
    m_scene = &scene;
    m_scenesLoaded = 0;
    m_runtime = std::make_unique<motor::SceneRuntime>(scene);
    m_runtime->SetAssets(m_assets);
    motor::log::Info("Play");
    m_runtime->Start();
    PauseOnError();
}

void PlayMode::Stop(motor::Scene& scene, motor::EntityId& selected) {
    if (!IsActive()) return;
    m_runtime->Stop();  // OnDestroy ainda vê a cena do jogo
    m_runtime.reset();
    m_scene = nullptr;
    scene = std::move(m_backup);
    m_backup = motor::Scene();
    if (!scene.Find(selected)) selected = scene.Find(m_backupSelected) ? m_backupSelected : motor::kNullEntity;
    m_paused = false;
    motor::log::Info("Parar: cena restaurada");
}

void PlayMode::SetPaused(bool paused) {
    if (!IsActive()) return;
    m_paused = paused;
    // Em pausa o runtime não roda: o áudio, que toca sozinho na thread de som, precisa ser pausado à parte.
    if (auto* audio = m_runtime->GetSystem<motor::AudioSystem>()) audio->SetPaused(paused);
}

void PlayMode::Update(float dt) {
    if (!IsActive() || m_paused) return;
    m_runtime->Update(dt);
    ApplySceneRequest();
    PauseOnError();
}

void PlayMode::FixedUpdate(float dt) {
    if (!IsActive() || m_paused) return;
    m_runtime->FixedUpdate(dt);
    ApplySceneRequest();
    PauseOnError();
}

void PlayMode::Step(float dt) {
    if (!IsActive() || !m_paused) return;
    m_runtime->FixedUpdate(dt);
    ApplySceneRequest();
    if (m_runtime && !m_runtime->TakeError()) {
        m_runtime->Update(dt);
        ApplySceneRequest();
    }
    if (m_runtime) m_runtime->TakeError();
}

void PlayMode::PauseOnError() {
    if (m_runtime && m_runtime->TakeError() && !m_paused) {
        SetPaused(true);
        motor::log::Warn("Play pausado por erro de execução (veja o erro acima no Console).");
    }
}

void PlayMode::ApplySceneRequest() {
    if (!m_runtime || !m_runtime->HasSceneRequest()) return;
    const std::string request = m_runtime->TakeSceneRequest();
    if (!m_loader) {
        motor::log::Warn("LoadScene(\"{}\"): troca de cena indisponível aqui", request);
        return;
    }
    // Carrega antes de parar: se falhar, o jogo continua na cena atual.
    motor::Scene next;
    std::string error;
    if (!m_loader(request, next, error)) {
        motor::log::Error("LoadScene(\"{}\"): {}", request, error);
        return;
    }
    const bool inputEnabled = m_runtime->InputEnabled();
    m_runtime->Stop();  // OnDestroy ainda vê a cena que sai
    const bool stopFailed = m_runtime->TakeError();
    m_runtime.reset();
    *m_scene = std::move(next);
    m_runtime = std::make_unique<motor::SceneRuntime>(*m_scene);
    m_runtime->SetAssets(m_assets);
    m_runtime->SetInputEnabled(inputEnabled);
    ++m_scenesLoaded;
    motor::log::Info("Cena trocada pelo jogo: {}", request);
    m_runtime->Start();
    if (stopFailed && !m_paused) {
        SetPaused(true);
        motor::log::Warn("Play pausado por erro de execução (veja o erro acima no Console).");
    }
    if (m_paused) {  // trocou de cena num "Avançar um quadro": os sons da cena nova esperam o Continuar
        if (auto* audio = m_runtime->GetSystem<motor::AudioSystem>()) audio->SetPaused(true);
    }
}

} // namespace editor
