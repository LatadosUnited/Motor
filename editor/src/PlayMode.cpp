#include "PlayMode.h"

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
    motor::log::Info("Play");
    m_runtime->Start();
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
    if (IsActive()) m_paused = paused;
}

void PlayMode::Update(float dt) {
    if (!IsActive() || m_paused) return;
    m_runtime->Update(dt);
    ApplySceneRequest();
}

void PlayMode::FixedUpdate(float dt) {
    if (!IsActive() || m_paused) return;
    m_runtime->FixedUpdate(dt);
    ApplySceneRequest();
}

void PlayMode::Step(float dt) {
    if (!IsActive() || !m_paused) return;
    m_runtime->FixedUpdate(dt);
    ApplySceneRequest();
    m_runtime->Update(dt);
    ApplySceneRequest();
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
    m_runtime.reset();
    *m_scene = std::move(next);
    m_runtime = std::make_unique<motor::SceneRuntime>(*m_scene);
    m_runtime->SetInputEnabled(inputEnabled);
    ++m_scenesLoaded;
    motor::log::Info("Cena trocada pelo jogo: {}", request);
    m_runtime->Start();
}

} // namespace editor
