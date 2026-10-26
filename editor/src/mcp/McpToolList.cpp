#include "McpToolList.h"

#include "../EditorContext.h"
#include "../EditorSettings.h"
#include "../UndoHistory.h"
#include "McpController.h"
#include "document/SceneDocument.h"
#include "export/ExportController.h"
#include "panels/ProjectPanel.h"
#include "play/GameView.h"
#include "play/PlayController.h"
#include "scripts/ScriptController.h"
#include "viewport/SceneViewport.h"

#include <motor/common/FileWrite.h>
#include <motor/mcp/HubLink.h>
#include <motor/core/Version.h>
#include <motor/scene/ScriptModule.h>

#include <stdexcept>

namespace editor::mcp {

Json EditorToolList() {
    // As mesmas peças e a mesma ordem do EditorApp. Nenhuma ferramenta roda aqui: só a lista é lida.
    EditorContext ctx;
    EditorSettings settings;
    ProjectPanel projectPanel;
    UndoHistory history;
    SceneViewport viewport;
    GameView gameView;
    PlayController play;
    SceneDocument document(ctx, history, settings, projectPanel, {});
    ScriptController scripts(ctx, history, document, play);
    ExportController exporter({.ctx = ctx, .document = document, .scripts = scripts});
    McpController controller({.ctx = ctx,
                              .document = document,
                              .play = play,
                              .scripts = scripts,
                              .viewport = viewport,
                              .gameView = gameView,
                              .requestHistory = [](int) {},
                              .exporter = exporter,
                              .renderer = []() -> motor::Renderer& {
                                  throw std::logic_error("--mcp-ferramentas não desenha");
                              }});

    Protocol& protocol = controller.GetProtocol();
    const Json init = *protocol.Handle(
        {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", {{"protocolVersion", Protocol::kLatestVersion}}}});
    const Json list = *protocol.Handle({{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}});
    return {{"format", kToolListFormat},
            {"version", kToolListVersion},
            {"engineVersion", motor::kVersion},
            {"scriptApiVersion", motor::kScriptApiVersion},
            {"hubLink", hublink::kVersion},  // este editor entende o --hub do Hub residente
            {"initialize", init["result"]},
            {"tools", list["result"]["tools"]}};
}

bool WriteEditorToolList(const std::filesystem::path& file, std::string& error) {
    return motor::WriteFileAtomic(file, EditorToolList().dump(2) + "\n", &error);
}

} // namespace editor::mcp
