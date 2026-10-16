#pragma once

#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>

#include <functional>
#include <memory>
#include <string>

namespace editor {

// Modo Play do editor. Play guarda uma cópia da cena e liga o loop de jogo (SceneRuntime) na própria cena
// do editor; Parar desliga o loop e devolve a cena exatamente como estava (tudo que mudou no Play some).
//
//   Edição --Play--> Jogando <--Pausa--> Pausado --Parar--> Edição
class PlayMode {
public:
    bool IsActive() const { return m_runtime != nullptr; }  // jogando ou pausado
    bool IsPaused() const { return m_paused; }

    void Play(motor::Scene& scene, motor::EntityId selected);
    // Restaura a cena. A seleção continua se a entidade existir na cena restaurada; senão volta a de antes.
    void Stop(motor::Scene& scene, motor::EntityId& selected);
    void SetPaused(bool paused);

    // Avançam o jogo; não fazem nada parado ou em pausa.
    void Update(float dt);
    void FixedUpdate(float dt);
    // Em pausa: roda um quadro (um passo fixo e um OnUpdate de `dt`).
    void Step(float dt);

    // Troca de cena pedida por um script (Script::LoadScene): carrega o pedido em `out` (false e `error` se
    // não deu). Sem carregador, o pedido é ignorado com aviso. A cena de antes do Play continua guardada:
    // Parar volta para ela, não para a última carregada.
    using SceneLoader = std::function<bool(const std::string& request, motor::Scene& out, std::string& error)>;
    void SetSceneLoader(SceneLoader loader) { m_loader = std::move(loader); }
    int ScenesLoaded() const { return m_scenesLoaded; }  // trocas feitas neste Play

    motor::SceneRuntime* Runtime() { return m_runtime.get(); }

private:
    void ApplySceneRequest();

    motor::Scene m_backup;
    motor::EntityId m_backupSelected = motor::kNullEntity;
    std::unique_ptr<motor::SceneRuntime> m_runtime;
    motor::Scene* m_scene = nullptr;  // a cena do editor, onde o jogo roda
    SceneLoader m_loader;
    int m_scenesLoaded = 0;
    bool m_paused = false;
};

} // namespace editor
