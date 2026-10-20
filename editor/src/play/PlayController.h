#pragma once

#include "EditorContext.h"
#include "play/PlayHost.h"
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
    // Viewport pela câmera do jogo: no Play, sem o modo lado a lado (nele o jogo fica na janela Jogo).
    bool InGameView() const { return m_mode.IsActive() && m_gameView && !m_sideBySide; }
    void ToggleGameView() { m_gameView = !m_gameView; }
    // Cena e Jogo lado a lado: o Viewport fica na câmera do editor e a janela Jogo (GameView) mostra o jogo.
    bool SideBySide() const { return m_sideBySide; }
    void SetSideBySide(bool on) { m_sideBySide = on; }
    // O jogo está com o teclado e o mouse neste quadro (calculado no Update).
    bool GameHasInput() const { return m_gameFocused; }
    ImU32 StateColor() const;  // azul jogando, laranja em pausa

    // Pedidos de menus, botões e atalhos são aplicados no início do próximo frame, como o desfazer.
    void Queue(Request request) { m_request = request; }
    Request Pending() const { return m_request; }  // pedido ainda não aplicado (None = já valeu)
    // O jogo recebe a entrada mesmo sem o viewport em foco (teclas simuladas pelo servidor MCP).
    void SetExternalInput(bool enabled) { m_externalInput = enabled; }
    Event ApplyRequest(EditorContext& ctx, UndoHistory& history);
    bool Stop(EditorContext& ctx);  // volta à edição; false se já estava editando
    // Do projeto aberto, para o próximo Play: opções padrão do jogador, versão do jogo e a pasta dos saves do Play
    // (<projeto>/.motor/saves; vazia sem projeto).
    struct GameInfo {
        motor::GameOptions options;
        std::string version;
        std::filesystem::path savesFolder;
        motor::InputActions actions = motor::InputActions::Defaults();
    };
    void SetGameInfo(GameInfo info) { m_gameInfo = std::move(info); }
    // Jogo > Apagar saves do Play: apaga os .json da pasta dos saves. Devolve quantos apagou.
    int DeletePlaySaves();
    const std::filesystem::path& PlaySavesFolder() const { return m_gameInfo.savesFolder; }
    motor::SaveStore* PlaySaves() { return m_mode.IsActive() ? &m_host.Saves() : nullptr; }
    // MSAA e sombras: as do jogo no Play (Script::ApplyOptions), as do editor fora dele. Antes de desenhar.
    void ApplyQuality(motor::Renderer& renderer) const;

    // `viewportFocused` e `gameWindowFocused`: o jogo só lê teclado/mouse com a janela que o mostra em foco (o
    // Viewport pela câmera do jogo, ou a janela Jogo no modo lado a lado). O cursor pedido pelo jogo
    // (SceneRuntime::GetCursorMode) vale nessas condições, fora da pausa; Esc solta e um clique prende de novo.
    void Update(float dt, bool viewportFocused, bool gameWindowFocused, motor::Window& window);
    void FixedUpdate(float dt) { m_mode.FixedUpdate(dt); }
    motor::SceneRuntime* Runtime() { return m_mode.Runtime(); }  // nullptr fora do Play
    void ForEachPlayScene(const std::function<void(motor::Scene&)>& fn) { m_mode.ForEachScene(fn); }
    const motor::Scene& BackupScene() const { return m_mode.BackupScene(); }

    void DrawMenu();       // menu "Jogo"
    void DrawStatus();     // "JOGANDO" / "EM PAUSA" na barra de menus
    void DrawControls(const ImVec2& origin, float viewportWidth);  // botões no topo do viewport

private:
    void UpdateCursor(motor::Window& window);
    PlayMode m_mode;
    PlayHost m_host;
    GameInfo m_gameInfo;
    Request m_request = Request::None;
    bool m_gameView = true;  // no Play: true = câmera do jogo, false = câmera livre do editor
    bool m_externalInput = false;
    bool m_sideBySide = false;
    bool m_gameFocused = false;
    bool m_cursorReleased = false;                          // Esc soltou o cursor preso pelo jogo
    motor::CursorMode m_appliedCursor = motor::CursorMode::Normal;  // o que o Play pôs na janela
};

} // namespace editor
