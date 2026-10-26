#pragma once

#include "../EditorContext.h"
#include "../EditorSettings.h"
#include "Mcp.h"

#include <motor/core/Input.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace editor {

class ExportController;
class PlayController;
class SceneDocument;
class SceneViewport;
class GameView;
class ScriptController;

// Servidor MCP do editor (Fase 13): deixa um agente (Claude Code...) ver e mexer no editor pelas mesmas
// operações da interface — cena, componentes, assets, Play, scripts, log e captura do viewport — sem simular
// mouse nem teclado do sistema. Liga/desliga em Ver > Servidor MCP ou com --mcp na linha de comando.
//
// As mensagens chegam numa thread do HTTP e são atendidas em Pump(), na thread principal, depois que o
// viewport desenhou o quadro: assim as ferramentas mexem na cena como um painel, e o UndoHistory::Track do
// fim do quadro transforma cada mudança num passo de desfazer.
class McpController {
public:
    struct Host {
        EditorContext& ctx;
        SceneDocument& document;
        PlayController& play;
        ScriptController& scripts;
        SceneViewport& viewport;
        GameView& gameView;  // janela Jogo (Cena e Jogo lado a lado)
        std::function<void(int)> requestHistory;  // -1 desfazer, +1 refazer (no início do próximo quadro)
        ExportController& exporter;               // game_export (Fase 10)
        std::function<motor::Renderer&()> renderer;  // viewport_capture desenha na hora (criado depois do editor)
    };

    explicit McpController(Host host);
    ~McpController();

    McpController(const McpController&) = delete;
    McpController& operator=(const McpController&) = delete;

    void Start(int port);  // erro vai para o log e para o popup do editor
    void Stop();
    bool IsRunning() const { return m_server.IsRunning(); }
    bool HasPending() const { return m_server.HasParked(); }  // pedido esperando: o editor não dorme

    void ApplyGameInput();  // início do quadro (antes do jogo rodar): teclas simuladas pedidas pelo agente
    bool Pump();            // depois do viewport desenhar: atende as mensagens (true se atendeu alguma)
    void OnPlayStopped();   // solta as teclas simuladas

    void DrawMenuItems(EditorSettings& settings);  // no menu Ver
    void DrawStatus();                             // "MCP" na barra de menus enquanto ligado

    mcp::Protocol& GetProtocol() { return m_protocol; }  // --mcp-ferramentas (McpToolList) e testes

private:
    struct LogLine {
        uint64_t n;
        motor::log::Level level;
        std::string text;
    };
    // Mudança no editor para events_read (o agente acompanha sem pedir o estado inteiro).
    struct Event {
        uint64_t n;
        std::string type;
        mcp::Json data;
    };
    // Estado comparado a cada 0,2 s para gerar os eventos.
    struct Watched {
        bool valid = false;
        std::vector<motor::EntityId> selection;
        std::string scenePath;
        bool unsaved = false;
        size_t entityCount = 0;
        std::string play;
        std::string scripts;
        size_t scriptErrors = 0;
    };
    struct HeldKey {
        motor::Key key;
        std::chrono::steady_clock::time_point releaseAt;  // max() = até pedirem para soltar
    };
    struct HeldPadButton {
        motor::GamepadButton button;
        std::chrono::steady_clock::time_point releaseAt;
        int pad = 0;
    };
    struct PendingPadButton {
        motor::GamepadButton button;
        bool down = false;
        int pad = 0;
    };
    struct HeldMouseButton {
        motor::MouseButton button;
        std::chrono::steady_clock::time_point releaseAt;
    };
    // Movimento do mouse (game_mouse move), espalhado em `seconds` pelo tempo de verdade: cada quadro anda o que
    // falta até onde já devia estar.
    struct MouseMove {
        glm::vec2 delta{0.0f};
        std::chrono::steady_clock::time_point start;
        double seconds = 0.0;
        glm::vec2 done{0.0f};
    };

    // Ferramentas por grupo: editor e vista em McpController.cpp, arquivos em McpFileTools.cpp, jogo e scripts
    // em McpPlayTools.cpp.
    void AddEditorTools();
    void AddSceneFileTools();
    void AddAssetTools();
    void AddPlayTools();
    void AddScriptTools();
    void AddViewTools();
    static std::string PlayState(const PlayController& play);  // "editando", "jogando" ou "pausado"
    std::filesystem::path ProjectPath(const mcp::Json& value) const;  // relativo ao projeto; erro se não houver projeto
    void UpdateUiClick();
    void DetectEvents();  // em Pump: compara com m_watched e acrescenta eventos
    void AddEvent(std::string type, mcp::Json data);

    Host m_host;
    mcp::Protocol m_protocol;
    mcp::Server m_server;

    // Últimas linhas do log (para log_read), preenchidas por um ouvinte do log de qualquer thread.
    int m_logSink = 0;
    std::mutex m_logMutex;
    std::deque<LogLine> m_log;
    uint64_t m_logCount = 0;

    std::deque<Event> m_events;  // os mais recentes (até 200)
    uint64_t m_eventCount = 0;
    Watched m_watched;
    std::chrono::steady_clock::time_point m_nextEventCheck{};

    // Entrada simulada no jogo: teclas a apertar/soltar no próximo quadro e as que estão presas.
    std::vector<std::pair<motor::Key, bool>> m_pendingKeys;
    std::vector<HeldKey> m_heldKeys;
    // Controle simulado (game_gamepad): botões a apertar/soltar no próximo quadro e os presos.
    std::vector<PendingPadButton> m_pendingPad;
    std::vector<HeldPadButton> m_heldPad;
    // Ligar/desligar controles simulados e mover eixos também esperam o começo do quadro: feito no meio dele, o
    // BeginFrame seguinte apagaria o WasGamepadConnected antes de o script ler. -1 = sem mudança; 0 = os de verdade.
    int m_pendingPadCount = -1;
    struct PendingAxis {
        motor::GamepadAxis axis;
        float value = 0.0f;
        int pad = 0;
    };
    std::vector<PendingAxis> m_pendingAxes;
    // Mouse e texto simulados (game_mouse, game_text): botões, movimentos em andamento, roda e texto do próximo
    // quadro.
    std::vector<std::pair<motor::MouseButton, bool>> m_pendingMouse;
    std::vector<HeldMouseButton> m_heldMouse;
    std::vector<MouseMove> m_mouseMoves;
    float m_pendingScroll = 0.0f;
    std::string m_pendingText;
    // Imagem do viewport_capture, desenhada na hora (com MSAA, como o Viewport).
    std::unique_ptr<motor::Framebuffer> m_capture;
    // Clique num botão da UI do jogo: posição na tela do jogo e etapa (1 = apertar, 2 = soltar, 0 = nada).
    int m_clickStage = 0;
    glm::vec2 m_clickPos{0.0f};
    glm::vec2 m_clickScreen{0.0f};
};

} // namespace editor
