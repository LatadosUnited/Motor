// Peça do editor com o servidor MCP: log, entrada do jogo, menu e as ferramentas do editor e da vista.
// Ferramentas de cena em McpSceneTools.cpp, de arquivos em McpFileTools.cpp, de jogo e scripts em
// McpPlayTools.cpp.

#include "McpController.h"

#include "export/ExportController.h"
#include "play/PlayController.h"
#include "document/SceneDocument.h"
#include "viewport/SceneViewport.h"
#include "scripts/ScriptController.h"
#include "ImageEncode.h"
#include "McpSceneTools.h"

#include <motor/core/Paths.h>
#include <motor/core/Version.h>
#include <motor/renderer/Framebuffer.h>
#include <motor/audio/AudioSystem.h>
#include <motor/ui/UiSystem.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <utility>

namespace editor {

using namespace motor;
using mcp::Json;
using mcp::Prop;
using mcp::Schema;
using mcp::Tool;
using mcp::ToolError;
using mcp::ToolResult;
namespace fs = std::filesystem;

namespace {
constexpr size_t kMaxLogLines = 1000;

constexpr const char* kInstructions =
    "Editor da engine Motor (C++/OpenGL). Ferramentas para ver e mudar a cena aberta como um usuário faria no "
    "editor. Comece com editor_status e scene_tree (ids das entidades). Cada mudança vira um passo de desfazer "
    "(undo/redo). Durante o Play (play) a cena é a do jogo: o que mudar é descartado ao parar. Para ver o "
    "resultado use viewport_capture; para testar o jogo use play, game_key e ui_click e leia log_read. Scripts "
    "ficam na pasta Scripts/ do projeto (edite os arquivos e chame scripts_build). Textos em português.";

const char* LevelName(log::Level level) {
    switch (level) {
        case log::Level::Trace: return "trace";
        case log::Level::Info: return "info";
        case log::Level::Warn: return "warn";
        case log::Level::Error: return "error";
    }
    return "?";
}
} // namespace

std::string McpController::PlayState(const PlayController& play) {
    if (!play.IsActive()) return "editando";
    return play.IsPaused() ? "pausado" : "jogando";
}

McpController::McpController(Host host)
    : m_host(std::move(host)), m_protocol("motor-editor", "0.13", kInstructions), m_server(m_protocol) {
    m_logSink = log::AddSink([this](log::Level level, std::string_view message) {
        std::lock_guard lock(m_logMutex);
        m_log.push_back({++m_logCount, level, std::string(message)});
        if (m_log.size() > kMaxLogLines) m_log.pop_front();
    });
    mcp::AddSceneTools(m_protocol, m_host.ctx);
    AddEditorTools();
    AddSceneFileTools();
    AddAssetTools();
    AddPlayTools();
    AddScriptTools();
    AddViewTools();
}

McpController::~McpController() {
    m_server.Stop();
    log::RemoveSink(m_logSink);
}

void McpController::Start(int port) {
    std::string error;
    if (!m_server.Start(port, error)) {
        log::Error("Servidor MCP: {}", error);
        m_host.ctx.pendingError = "Servidor MCP: " + error;
    }
}

void McpController::Stop() {
    m_server.Stop();
}

fs::path McpController::ProjectPath(const Json& value) const {
    const Project* project = m_host.document.GetProject();
    if (!project) throw ToolError("nenhum projeto aberto");
    if (!value.is_string() || value.get<std::string>().empty()) throw ToolError("informe o caminho do arquivo");
    fs::path path = PathFromUtf8(value.get<std::string>());
    if (!path.is_absolute()) path = project->RootDir() / path;
    return path.lexically_normal().make_preferred();
}

void McpController::ApplyGameInput() {
    const auto now = std::chrono::steady_clock::now();
    // Soltas primeiro as que venceram (apertadas num quadro anterior: o jogo viu a tecla descer).
    std::erase_if(m_heldKeys, [&](const HeldKey& h) {
        if (now < h.releaseAt) return false;
        Input::SetKeyState(h.key, false);
        return true;
    });
    for (const auto& [key, down] : m_pendingKeys) Input::SetKeyState(key, down);
    m_pendingKeys.clear();
    m_host.play.SetExternalInput(!m_heldKeys.empty() || m_clickStage != 0);
}

void McpController::OnPlayStopped() {
    for (const HeldKey& h : m_heldKeys) Input::SetKeyState(h.key, false);
    m_heldKeys.clear();
    m_pendingKeys.clear();
    m_clickStage = 0;
    m_host.play.SetExternalInput(false);
}

void McpController::UpdateUiClick() {
    if (m_clickStage == 0) return;
    SceneRuntime* runtime = m_host.play.Runtime();
    UiSystem* ui = runtime ? runtime->GetSystem<UiSystem>() : nullptr;
    if (!ui) {
        m_clickStage = 0;
        return;
    }
    // Sobrescreve o mouse que o viewport informou neste quadro: aperta num quadro, solta no seguinte.
    ui->SetPointer({m_clickScreen, m_clickPos, true, m_clickStage == 1});
    m_clickStage = m_clickStage == 1 ? 2 : 0;
}

void McpController::Pump() {
    m_server.Pump();
    UpdateUiClick();
}

void McpController::DrawMenuItems(EditorSettings& settings) {
    const std::string label = std::format("Servidor MCP (porta {})", settings.mcpPort);
    if (ImGui::MenuItem(label.c_str(), nullptr, IsRunning())) {
        if (IsRunning()) {
            Stop();
            settings.mcpEnabled = false;
        } else {
            Start(settings.mcpPort);
            settings.mcpEnabled = IsRunning();
        }
    }
    if (IsRunning() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Agentes (Claude Code...) controlam o editor por %s\n"
                          "claude mcp add --transport http motor %s",
                          m_server.Url().c_str(), m_server.Url().c_str());
    }
}

void McpController::DrawStatus() {
    if (!IsRunning()) return;
    ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "MCP");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Servidor MCP ligado em %s", m_server.Url().c_str());
}

// --- Editor: situação, desfazer, salvar/abrir cena, log -------------------------------------------------

void McpController::AddEditorTools() {
    m_protocol.AddTool({
        .name = "editor_status",
        .description = "Situação do editor: projeto e cena abertos, alterações não salvas, estado do Play, "
                       "entidade selecionada e situação dos scripts. Bom primeiro passo.",
        .inputSchema = Schema(),
        .readOnly = true,
        .run = [this](const Json&) {
            Json j;
            if (Project* project = m_host.document.GetProject()) {
                j["project"] = {{"name", project->Name()},
                                {"root", ToUtf8(project->RootDir())},
                                {"engine_version", project->EngineVersion()}};
            } else {
                j["project"] = nullptr;
            }
            j["editor_version"] = kVersion;
            const fs::path& scene = m_host.document.ScenePath();
            j["scene"] = {{"path", scene.empty() ? Json(nullptr) : Json(ToUtf8(scene))},
                          {"unsaved_changes", m_host.document.HasUnsavedChanges()},
                          {"entity_count", m_host.ctx.scene.Entities().size()}};
            j["play"] = PlayState(m_host.play);
            j["selected"] = m_host.ctx.selected;
            j["selection"] = m_host.ctx.selection;
            const ScriptController::Status s = m_host.scripts.GetStatus();
            size_t errors = 0;
            for (const auto& d : s.diagnostics) errors += d.kind == ScriptProject::Diagnostic::Kind::Error;
            j["scripts"] = {{"state", s.state}, {"errors", errors}, {"loaded", s.loadedTypes}};
            if (const Framebuffer* fb = m_host.viewport.Target()) j["viewport"] = {fb->Width(), fb->Height()};
            return ToolResult::Data(j);
        },
    });

    m_protocol.AddTool({
        .name = "undo",
        .description = "Desfaz a última alteração da cena (como Ctrl+Z). Não funciona durante o Play.",
        .inputSchema = Schema(),
        .run = [this](const Json&) {
            if (m_host.play.IsActive()) throw ToolError("não há desfazer durante o Play (pare antes)");
            m_host.requestHistory(-1);
            return ToolResult::Text("desfazer pedido (aplicado no próximo quadro)");
        },
    });
    m_protocol.AddTool({
        .name = "redo",
        .description = "Refaz a alteração desfeita (como Ctrl+Y).",
        .inputSchema = Schema(),
        .run = [this](const Json&) {
            if (m_host.play.IsActive()) throw ToolError("não há refazer durante o Play (pare antes)");
            m_host.requestHistory(+1);
            return ToolResult::Text("refazer pedido (aplicado no próximo quadro)");
        },
    });

    m_protocol.AddTool({
        .name = "log_read",
        .description = "Últimas linhas do log do editor (o mesmo do console): erros de cena, mensagens dos "
                       "scripts (Log), compilação. Use 'since' com o número da última linha lida para ver só as "
                       "novas.",
        .inputSchema = Schema({{"lines", Prop("integer", "quantas linhas, as mais recentes (padrão 40, máx. 500)")},
                               {"since", Prop("integer", "só linhas depois deste número")},
                               {"min_level", {{"type", "string"}, {"enum", {"info", "warn", "error"}}}}}),
        .readOnly = true,
        .run = [this](const Json& args) {
            const size_t count = std::clamp<int64_t>(args.value("lines", int64_t{40}), 1, 500);
            const uint64_t since = args.value("since", uint64_t{0});
            const std::string minName = args.value("min_level", std::string("info"));
            const log::Level min = minName == "error" ? log::Level::Error
                                   : minName == "warn" ? log::Level::Warn
                                                       : log::Level::Info;
            std::vector<std::string> out;
            uint64_t last = 0;
            {
                std::lock_guard lock(m_logMutex);
                for (auto it = m_log.rbegin(); it != m_log.rend() && out.size() < count; ++it) {
                    if (it->n <= since) break;
                    if (it->level < min) continue;
                    out.push_back(std::format("#{} [{}] {}", it->n, LevelName(it->level), it->text));
                }
                last = m_logCount;
            }
            std::reverse(out.begin(), out.end());
            std::string text = std::format("última linha: #{}\n", last);
            for (const std::string& line : out) text += line + "\n";
            if (out.empty()) text += "(nada novo)\n";
            return ToolResult::Text(std::move(text));
        },
    });
}

// --- Viewport: captura e foco ---------------------------------------------------------------------------

void McpController::AddViewTools() {
    m_protocol.AddTool({
        .name = "viewport_capture",
        .description = "Imagem (PNG) do viewport no último quadro: a cena com o HUD do jogo, pela câmera do editor "
                       "ou, no Play, pela câmera do jogo. Sem os botões e gizmos do editor.",
        .inputSchema = Schema({{"max_width", Prop("integer", "largura máxima em pixels (padrão 800)")},
                               {"max_height", Prop("integer", "altura máxima em pixels (padrão 600)")}}),
        .readOnly = true,
        .run = [this](const Json& args) {
            const Framebuffer* fb = m_host.viewport.Target();
            if (!fb || fb->Width() < 2 || fb->Height() < 2) throw ToolError("o viewport ainda não foi desenhado");
            mcp::Image image{fb->Width(), fb->Height(), fb->ReadPixels()};
            const int maxW = std::clamp<int>(args.value("max_width", 800), 64, 2048);
            const int maxH = std::clamp<int>(args.value("max_height", 600), 64, 2048);
            image = mcp::Downscale(image, maxW, maxH);
            ToolResult r = ToolResult::Text(std::format("viewport {}x{} (imagem {}x{}), {}", fb->Width(), fb->Height(),
                                                        image.width, image.height, PlayState(m_host.play)));
            r.AddImage(mcp::Base64(mcp::EncodePng(image)), "image/png");
            return r;
        },
    });

    m_protocol.AddTool({
        .name = "view_focus",
        .description = "Seleciona uma entidade e aproxima a câmera do editor dela (como a tecla F), para capturar "
                       "o viewport em seguida.",
        .inputSchema = Schema({{"entity", {{"type", {"integer", "string"}}, {"description", "id ou nome"}}}},
                              {"entity"}),
        .run = [this](const Json& args) {
            Entity& e = mcp::ResolveEntity(m_host.ctx, args);
            m_host.ctx.selected = e.id;
            m_host.viewport.FocusSelected(m_host.ctx);
            return ToolResult::Text("câmera do editor focando " + e.name);
        },
    });
}

} // namespace editor
