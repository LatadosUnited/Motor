#pragma once

#include "EditorContext.h"
#include "play/PlayMode.h"
#include "UndoHistory.h"

#include <imgui.h>

namespace editor {

// Interface do modo Play no editor: menu "Jogo", botões no viewport, câmera do jogo/editor e pedidos de
// Play/Pausa/Parar. A lógica de copiar e restaurar a cena fica em PlayMode (testável sem janela).
class PlayController {
public:
    enum class Request { None, Play, Pause, Stop, Step };
    enum class Event { None, Started, Stopped };  // resultado de ApplyRequest, para o editor reagir

    bool IsActive() const { return m_mode.IsActive(); }
    bool IsPaused() const { return m_mode.IsPaused(); }
    bool InGameView() const { return m_mode.IsActive() && m_gameView; }  // viewport pela câmera do jogo
    void ToggleGameView() { m_gameView = !m_gameView; }
    ImU32 StateColor() const;  // azul jogando, laranja em pausa

    // Pedidos de menus, botões e atalhos são aplicados no início do próximo frame, como o desfazer.
    void Queue(Request request) { m_request = request; }
    // O jogo recebe a entrada mesmo sem o viewport em foco (teclas simuladas pelo servidor MCP).
    void SetExternalInput(bool enabled) { m_externalInput = enabled; }
    Event ApplyRequest(EditorContext& ctx, UndoHistory& history);
    bool Stop(EditorContext& ctx);  // volta à edição; false se já estava editando

    // `viewportFocused`: o jogo só lê teclado/mouse vendo pela câmera do jogo com o viewport em foco. O cursor
    // pedido pelo jogo (SceneRuntime::GetCursorMode) vale nessas condições, fora da pausa; Esc solta e um clique
    // no viewport prende de novo.
    void Update(float dt, bool viewportFocused, motor::Window& window);
    void FixedUpdate(float dt) { m_mode.FixedUpdate(dt); }
    motor::SceneRuntime* Runtime() { return m_mode.Runtime(); }  // nullptr fora do Play
    void ForEachPlayScene(const std::function<void(motor::Scene&)>& fn) { m_mode.ForEachScene(fn); }
    const motor::Scene& BackupScene() const { return m_mode.BackupScene(); }

    void DrawMenu();       // menu "Jogo"
    void DrawStatus();     // "JOGANDO" / "EM PAUSA" na barra de menus
    void DrawControls(const ImVec2& origin, float viewportWidth);  // botões no topo do viewport

private:
    void UpdateCursor(bool viewportFocused, motor::Window& window);
    PlayMode m_mode;
    Request m_request = Request::None;
    bool m_gameView = true;  // no Play: true = câmera do jogo, false = câmera livre do editor
    bool m_externalInput = false;
    bool m_cursorReleased = false;                          // Esc soltou o cursor preso pelo jogo
    motor::CursorMode m_appliedCursor = motor::CursorMode::Normal;  // o que o Play pôs na janela
};

} // namespace editor
