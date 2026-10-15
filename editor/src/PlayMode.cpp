#include "PlayMode.h"

#include <motor/core/Log.h>

#include <utility>

namespace editor {

void PlayMode::Play(motor::Scene& scene, motor::EntityId selected) {
    if (IsActive()) return;
    m_backup = scene;  // cópia profunda: os scripts copiados começam sem dono e não iniciados
    m_backupSelected = selected;
    m_paused = false;
    m_runtime = std::make_unique<motor::SceneRuntime>(scene);
    motor::log::Info("Play");
    m_runtime->Start();
}

void PlayMode::Stop(motor::Scene& scene, motor::EntityId& selected) {
    if (!IsActive()) return;
    m_runtime->Stop();  // OnDestroy ainda vê a cena do jogo
    m_runtime.reset();
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
    if (IsActive() && !m_paused) m_runtime->Update(dt);
}

void PlayMode::FixedUpdate(float dt) {
    if (IsActive() && !m_paused) m_runtime->FixedUpdate(dt);
}

void PlayMode::Step(float dt) {
    if (!IsActive() || !m_paused) return;
    m_runtime->FixedUpdate(dt);
    m_runtime->Update(dt);
}

} // namespace editor
