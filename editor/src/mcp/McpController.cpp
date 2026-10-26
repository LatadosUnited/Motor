// Peça do editor com o servidor MCP: log, entrada do jogo, menu e as ferramentas do editor e da vista.
// Ferramentas de cena em McpSceneTools.cpp, de arquivos em McpFileTools.cpp, de jogo e scripts em
// McpPlayTools.cpp.

#include "McpController.h"

#include "export/ExportController.h"
#include "play/GameView.h"
#include "play/PlayController.h"
#include "document/SceneDocument.h"
#include "viewport/SceneViewport.h"
#include "scripts/ScriptController.h"
#include "scripts/ScriptProject.h"
#include "ImageEncode.h"
#include "McpSceneTools.h"

#include <motor/mcp/HubLink.h>

#include <motor/core/EngineAssets.h>
#include <motor/core/FrameStats.h>
#include <motor/core/Paths.h>
#include <motor/core/Window.h>
#include <motor/core/Version.h>
#include <motor/renderer/Framebuffer.h>
#include <motor/audio/AudioSystem.h>
#include <motor/scene/ScriptModule.h>
#include <motor/ui/UiSystem.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
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
    "resultado use viewport_capture (view_camera move a câmera); para testar o jogo use play, game_key e "
    "ui_click e leia log_read. events_read conta o que mudou no editor desde a última leitura. Scripts "
    "ficam na pasta Scripts/ do projeto (edite os arquivos e chame scripts_build com wait = true). Antes de "
    "escrever scripts, leia a API desta versão com docs_read ou nas pastas que editor_status mostra em "
    "script_reference (documentação e headers do SDK desta versão; cópias em outras pastas, como a do projeto, "
    "podem ser de outra versão). Textos em português.";

// Contadores do último quadro (Fase 22) para o editor_status. Arredondados: é para ler, não para somar.
Json PerfJson(const FrameStats::Frame& frame) {
    auto round = [](double v) { return std::round(v * 1000.0) / 1000.0; };
    auto entries = [&](const std::vector<FrameStats::Entry>& list) {
        Json o = Json::object();
        for (const auto& e : list) o[e.name] = round(e.value);
        return o;
    };
    return {{"frame", frame.index}, {"frame_ms", round(frame.frameMs)}, {"cpu_ms", entries(frame.cpu)},
            {"gpu_ms", entries(frame.gpu)}, {"values", entries(frame.values)}};
}

// Headers que os scripts incluem, da versão que está rodando: os do SDK instalado (sdk/include ao lado do
// editor) ou, num build do código-fonte, engine/include.
fs::path ScriptHeadersDir() {
    const fs::path sdk = ScriptProject::DefaultSdkJson().parent_path() / "include";
    std::error_code ec;
    if (fs::is_regular_file(sdk / "motor" / "scene" / "Script.h", ec)) return sdk;
    return SourceEngineAssetsDir().parent_path() / "include";
}

// Seções de um Markdown cujo título contém `query` (sem diferença de maiúsculas), cada uma até o próximo
// título do mesmo nível ou acima; com `query` vazio, a lista de títulos.
std::string DocSections(const std::string& text, const std::string& query) {
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return s;
    };
    std::vector<std::string> lines;
    for (size_t start = 0; start <= text.size();) {
        const size_t end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    auto level = [](const std::string& line) {
        size_t n = 0;
        while (n < line.size() && line[n] == '#') ++n;
        return n > 0 && n < line.size() && line[n] == ' ' ? int(n) : 0;
    };
    std::string out;
    bool inCode = false;
    int open = 0;  // nível da seção sendo copiada (0 = nenhuma)
    const std::string wanted = lower(query);
    for (const std::string& line : lines) {
        if (line.starts_with("```")) inCode = !inCode;
        const int l = inCode ? 0 : level(line);
        if (query.empty()) {
            if (l > 0) out += line + "\n";
            continue;
        }
        if (l > 0 && open > 0 && l <= open) open = 0;
        if (l > 0 && open == 0 && lower(line).find(wanted) != std::string::npos) open = l;
        if (open > 0) out += line + "\n";
    }
    if (query.empty()) return "Títulos (peça um com section):\n" + out;
    if (out.empty()) return "nenhuma seção com \"" + query + "\" no título (sem section, a lista de títulos)";
    return out;
}

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
    mcp::AddSceneTools(m_protocol, m_host.ctx, [this] { return m_host.play.Runtime(); });
    mcp::AddNavTools(m_protocol, m_host.ctx, [this] { return m_host.play.Runtime(); });
    AddEditorTools();
    AddSceneFileTools();
    AddAssetTools();
    AddPlayTools();
    AddScriptTools();
    AddViewTools();
}

McpController::~McpController() {
    Stop();
    log::RemoveSink(m_logSink);
}

void McpController::LinkHub(int hubPort, std::string token) {
    m_hubPort = hubPort;
    m_hubToken = std::move(token);
    m_server.SetToken(m_hubToken);  // a porta interna só atende o Hub
}

void McpController::Start(int port) {
    std::string error;
    m_server.SetWake(&motor::Window::Wake);
    m_server.SetLog([](const std::string& message) { log::Info("{}", message); });
    if (!m_server.Start(port, error)) {
        log::Error("Servidor MCP: {}", error);
        m_host.ctx.pendingError = "Servidor MCP: " + error;
        return;
    }
    if (HubLinked()) NotifyHub(mcp::hublink::kEventReady);
}

void McpController::Stop() {
    if (m_hubAnnounced) NotifyHub(mcp::hublink::kEventClosing);  // antes de a porta fechar
    m_server.Stop();
}

std::string McpController::CurrentProjectUtf8() {
    const Project* project = m_host.document.GetProject();
    return project ? ToUtf8(project->RootDir()) : std::string();
}

void McpController::NotifyHub(const char* event) {
    const std::string project = CurrentProjectUtf8();
    const Json body = {{"event", event}, {"port", m_server.Port()}, {"project", project}};
    std::string error;
    // Loopback: responde em milissegundos; o tempo limite curto não deixa um Hub travado segurar o editor.
    if (!mcp::hublink::PostJson(m_hubPort, mcp::hublink::kNotifyPath, m_hubToken, body, &error, 1000)) {
        log::Warn("Hub do Motor não recebeu o aviso '{}': {}", event, error);
    }
    m_announcedProject = project;
    m_hubAnnounced = std::string_view(event) != mcp::hublink::kEventClosing;
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
    if (m_pendingPadCount >= 0) {
        Input::SetGamepadSimulated(m_pendingPadCount > 0, m_pendingPadCount);
        m_pendingPadCount = -1;
    }
    for (const PendingAxis& a : m_pendingAxes) Input::SetGamepadAxisState(a.axis, a.value, a.pad);
    m_pendingAxes.clear();
    std::erase_if(m_heldPad, [&](const HeldPadButton& h) {
        if (now < h.releaseAt) return false;
        Input::SetGamepadButtonState(h.button, false, h.pad);
        return true;
    });
    for (const PendingPadButton& p : m_pendingPad) Input::SetGamepadButtonState(p.button, p.down, p.pad);
    m_pendingPad.clear();
    // Mouse: botões como as teclas; movimento até onde já devia estar; roda e texto uma vez.
    std::erase_if(m_heldMouse, [&](const HeldMouseButton& h) {
        if (now < h.releaseAt) return false;
        Input::SetMouseButtonState(h.button, false);
        return true;
    });
    for (const auto& [button, down] : m_pendingMouse) Input::SetMouseButtonState(button, down);
    m_pendingMouse.clear();
    std::erase_if(m_mouseMoves, [&](MouseMove& m) {
        const double elapsed = std::chrono::duration<double>(now - m.start).count();
        const float progress = m.seconds <= 0.0 ? 1.0f : static_cast<float>(std::min(elapsed / m.seconds, 1.0));
        const glm::vec2 target = m.delta * progress;
        Input::SetMousePosition(Input::MousePosition() + (target - m.done));
        m.done = target;
        return progress >= 1.0f;
    });
    if (m_pendingScroll != 0.0f) Input::AddScroll(std::exchange(m_pendingScroll, 0.0f));
    if (!m_pendingText.empty()) Input::AddTextInput(std::exchange(m_pendingText, {}));
    m_host.play.SetExternalInput(!m_heldKeys.empty() || !m_heldMouse.empty() || !m_mouseMoves.empty() ||
                                 m_clickStage != 0 || Input::IsGamepadSimulated());
}

void McpController::OnPlayStopped() {
    for (const HeldKey& h : m_heldKeys) Input::SetKeyState(h.key, false);
    m_heldKeys.clear();
    m_pendingKeys.clear();
    m_heldPad.clear();
    m_pendingPad.clear();
    m_pendingPadCount = -1;
    m_pendingAxes.clear();
    for (const HeldMouseButton& h : m_heldMouse) Input::SetMouseButtonState(h.button, false);
    m_heldMouse.clear();
    m_pendingMouse.clear();
    m_mouseMoves.clear();
    m_pendingScroll = 0.0f;
    m_pendingText.clear();
    Input::SetGamepadSimulated(false);  // volta o controle de verdade
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

bool McpController::Pump() {
    const bool handled = m_server.Pump() > 0;
    UpdateUiClick();
    if (IsRunning()) DetectEvents();
    // Arquivo > Abrir projeto no mesmo editor: o Hub passa a mandar /mcpengine/<outro projeto> para cá.
    if (m_hubAnnounced && CurrentProjectUtf8() != m_announcedProject) NotifyHub(mcp::hublink::kEventProject);
    return handled;
}

void McpController::AddEvent(std::string type, Json data) {
    m_events.push_back({++m_eventCount, std::move(type), std::move(data)});
    if (m_events.size() > 200) m_events.pop_front();
}

void McpController::DetectEvents() {
    // A cada 0,2 s: comparar a cena com a salva e ler o estado dos scripts custa demais para todo quadro.
    const auto now = std::chrono::steady_clock::now();
    if (now < m_nextEventCheck) return;
    m_nextEventCheck = now + std::chrono::milliseconds(200);

    Watched w;
    w.valid = true;
    w.selection = m_host.ctx.selection;
    w.scenePath = ToUtf8(m_host.document.ScenePath());
    w.unsaved = m_host.document.HasUnsavedChanges();
    w.entityCount = m_host.ctx.scene.Entities().size();
    w.play = PlayState(m_host.play);
    const ScriptController::Status s = m_host.scripts.GetStatus();
    w.scripts = s.state;
    for (const auto& d : s.diagnostics) w.scriptErrors += d.kind == ScriptProject::Diagnostic::Kind::Error;

    const Watched& old = m_watched;
    if (old.valid) {  // a primeira leitura só guarda o estado
        if (w.scenePath != old.scenePath) {
            AddEvent("scene_opened", {{"path", w.scenePath}});
        } else if (old.unsaved && !w.unsaved) {
            AddEvent("scene_saved", {{"path", w.scenePath}});
        } else if (!old.unsaved && w.unsaved) {
            AddEvent("scene_modified", Json::object());
        }
        if (w.entityCount != old.entityCount) {
            AddEvent("entities_changed", {{"count", w.entityCount}, {"before", old.entityCount}});
        }
        if (w.selection != old.selection) {
            AddEvent("selection_changed", {{"selected", m_host.ctx.selected}, {"selection", w.selection}});
        }
        if (w.play != old.play) AddEvent("play", {{"state", w.play}});
        if (w.scripts != old.scripts || w.scriptErrors != old.scriptErrors) {
            AddEvent("scripts", {{"state", w.scripts}, {"errors", w.scriptErrors}});
        }
    }
    m_watched = std::move(w);
}

void McpController::DrawMenuItems(EditorSettings& settings) {
    if (HubLinked()) {
        // Aberto pelo Hub: o servidor é a ligação do Hub com este editor (/mcpengine/<Projeto>), sempre ligado.
        ImGui::MenuItem("Servidor MCP (pelo Hub do Motor)", nullptr, IsRunning(), false);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Aberto pelo Hub do Motor: os agentes chegam a este editor pelo endereço do Hub\n"
                              "(porta interna %d).", m_server.Port());
        }
        return;
    }
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
                       "entidade selecionada e situação dos scripts. Bom primeiro passo. Em perf, os contadores do "
                       "último quadro: duração, ms de CPU por parte (loop, sistemas, scripts), ms de GPU por passada "
                       "do renderer e números (draws, triângulos).",
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
            j["script_api"] = kScriptApiVersion;  // a mesma de docs_read
            // Onde ler a API desta versão com as ferramentas do próprio agente (a fonte final são os headers).
            auto path = [](const fs::path& p) { return ToUtf8(p.lexically_normal().make_preferred()); };
            const fs::path docs = EngineDocsDir();
            const fs::path headers = ScriptHeadersDir();
            j["script_reference"] = {{"docs", path(docs)},
                                     {"api", path(docs / "scripts-api.md")},
                                     {"guide", path(docs / "scripts.md")},
                                     {"headers", path(headers)},
                                     {"script_h", path(headers / "motor" / "scene" / "Script.h")}};
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
            j["perf"] = PerfJson(FrameStats::Instance().Last());
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
        .name = "events_read",
        .description = "O que mudou no editor desde a última leitura, em ordem: selection_changed, scene_opened, "
                       "scene_saved, scene_modified, entities_changed, play (editando/jogando/pausado) e scripts "
                       "(compilando/ok/falhou, com erros). Inclui o que o usuário fez à mão. Use 'since' com o "
                       "número do último evento lido para ver só os novos (a lista guarda os 200 mais recentes).",
        .inputSchema = Schema({{"since", Prop("integer", "só eventos depois deste número (padrão 0: todos os guardados)")}}),
        .readOnly = true,
        .run = [this](const Json& args) {
            const uint64_t since = args.value("since", uint64_t{0});
            m_nextEventCheck = {};
            DetectEvents();  // o que mudou desde a última verificação também entra
            Json list = Json::array();
            for (const Event& e : m_events) {
                if (e.n > since) list.push_back({{"n", e.n}, {"type", e.type}, {"data", e.data}});
            }
            return ToolResult::Data({{"last", m_eventCount}, {"events", std::move(list)}});
        },
    });

    m_protocol.AddTool({
        .name = "docs_read",
        .description = "Documentação de scripts DESTA versão do editor (a mesma API que o compilador usa; os "
                       "arquivos e os headers do SDK estão nas pastas de editor_status, em script_reference): "
                       "doc = api (referência: todas as funções, campos e componentes) ou guide (guia com "
                       "exemplos). section devolve só as seções cujo título contém o texto (ex.: \"Entrada\", "
                       "\"Animação\", \"Saves\"); sem section, a lista de títulos. all = true devolve o arquivo "
                       "inteiro.",
        .inputSchema = Schema({{"doc", {{"type", "string"}, {"enum", {"api", "guide"}},
                                        {"description", "api (padrão): scripts-api.md; guide: scripts.md"}}},
                               {"section", Prop("string", "parte do título da seção (sem diferença de maiúsculas)")},
                               {"all", Prop("boolean", "o arquivo inteiro (padrão: false)")}}),
        .readOnly = true,
        .run = [](const Json& args) {
            const std::string doc = args.value("doc", std::string("api"));
            if (doc != "api" && doc != "guide") throw ToolError("doc: use api ou guide");
            const fs::path file = EngineDocsDir() / (doc == "api" ? "scripts-api.md" : "scripts.md");
            std::ifstream in(file, std::ios::binary);
            if (!in) throw ToolError("documentação não encontrada: " + ToUtf8(file));
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const std::string header = std::format("{} (Motor {}, API de scripts {})\n\n", ToUtf8(file.filename()),
                                                   kVersion, kScriptApiVersion);
            if (args.value("all", false)) return ToolResult::Text(header + text);
            return ToolResult::Text(header + DocSections(text, args.value("section", std::string())));
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
        .description = "Imagem (PNG) da cena desenhada na hora, mesmo com o editor sem foco, minimizado ou com a "
                       "janela escondida: a cena com o HUD do jogo, sem os botões e gizmos do editor. view = scene "
                       "(padrão): como o Viewport mostra (câmera do editor; no Play, a do jogo). view = game: pela "
                       "câmera principal da cena, como o jogo vê (no Play ou fora dele; não precisa da janela Jogo). "
                       "view = editor: sempre pela câmera do editor (no Play também).",
        .inputSchema = Schema({{"max_width", Prop("integer", "largura máxima em pixels (padrão 800)")},
                               {"max_height", Prop("integer", "altura máxima em pixels (padrão 600)")},
                               {"view", {{"type", "string"}, {"enum", {"scene", "game", "editor"}},
                                         {"description", "scene (padrão), game ou editor"}}}}),
        .readOnly = true,
        .run = [this](const Json& args) {
            const std::string view = args.value("view", std::string("scene"));
            if (view != "scene" && view != "game" && view != "editor") throw ToolError("view: use scene, game ou editor");
            const int maxW = std::clamp<int>(args.value("max_width", 800), 64, 2048);
            const int maxH = std::clamp<int>(args.value("max_height", 600), 64, 2048);
            // Tamanho da janela que mostra essa vista (a proporção da imagem é a dela), reduzido ao máximo pedido.
            glm::vec2 size = view == "game" ? m_host.gameView.Size() : glm::vec2(0.0f);
            if (size.x < 2.0f || size.y < 2.0f) size = m_host.viewport.Size();
            if (size.x < 2.0f || size.y < 2.0f) size = {1280.0f, 720.0f};  // o Viewport nunca foi desenhado
            const float scale = std::min({1.0f, maxW / size.x, maxH / size.y});
            const int width = std::max(2, static_cast<int>(size.x * scale));
            const int height = std::max(2, static_cast<int>(size.y * scale));

            if (!m_capture) m_capture = std::make_unique<Framebuffer>(width, height, 4);
            m_capture->Resize(width, height);
            Renderer& renderer = m_host.renderer();
            bool hasCamera = true;
            if (view == "game") {
                hasCamera = m_host.gameView.RenderTo(renderer, m_host.ctx, *m_capture);
            } else {
                hasCamera = m_host.viewport.RenderTo(renderer, m_host.ctx, view == "scene" && m_host.play.InGameView(),
                                                     *m_capture);
            }
            if (!hasCamera) throw ToolError("a cena não tem câmera ativa (adicione o componente Câmera) para a vista do jogo");
            mcp::Image image{m_capture->Width(), m_capture->Height(), m_capture->ReadPixels()};
            ToolResult r = ToolResult::Text(std::format("{} {}x{}, desenhada agora, {}", view, image.width, image.height,
                                                        PlayState(m_host.play)));
            r.AddImage(mcp::Base64(mcp::EncodePng(image)), "image/png");
            return r;
        },
    });

    m_protocol.AddTool({
        .name = "view_camera",
        .description = "Câmera do editor (a do viewport fora do Play). Sem argumentos devolve onde ela está. "
                       "position move a câmera para o ponto; look_at a vira para um ponto (mantendo a posição se "
                       "position não vier). Para enquadrar uma entidade, view_focus é mais simples.",
        .inputSchema = Schema({{"position", Prop("array", "posição [x, y, z] no mundo")},
                               {"look_at", Prop("array", "ponto [x, y, z] para onde olhar")}}),
        .run = [this](const Json& args) {
            auto vec3 = [](const Json& v, const char* name) {
                if (!v.is_array() || v.size() != 3 || !v[0].is_number() || !v[1].is_number() || !v[2].is_number()) {
                    throw ToolError(std::format("'{}' deve ser [x, y, z]", name));
                }
                return glm::vec3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
            };
            glm::vec3 position = m_host.viewport.EditorCamera().position;
            if (args.contains("position")) position = vec3(args["position"], "position");
            std::optional<glm::vec3> target;
            if (args.contains("look_at")) target = vec3(args["look_at"], "look_at");
            if (target && glm::length(*target - position) < 1e-4f) {
                throw ToolError("look_at precisa ser diferente da posição da câmera");
            }
            if (args.contains("position") || target) m_host.viewport.SetEditorCamera(position, target);
            const motor::Camera& cam = m_host.viewport.EditorCamera();
            const glm::vec3 forward = cam.Forward();
            return ToolResult::Data({{"position", {cam.position.x, cam.position.y, cam.position.z}},
                                     {"forward", {forward.x, forward.y, forward.z}},
                                     {"yaw", cam.yaw},
                                     {"pitch", cam.pitch}});
        },
    });

    m_protocol.AddTool({
        .name = "view_side_by_side",
        .description = "Liga ou desliga Cena e Jogo lado a lado (menu Jogo): a janela Jogo mostra o jogo e recebe a "
                       "entrada; o Viewport fica na câmera do editor. Capture com viewport_capture view = game.",
        .inputSchema = Schema({{"enabled", Prop("boolean", "true liga, false desliga")}}, {"enabled"}),
        .run = [this](const Json& args) {
            const bool on = args.value("enabled", false);
            m_host.play.SetSideBySide(on);
            return ToolResult::Text(on ? "Cena e Jogo lado a lado: ligado (a janela aparece no próximo quadro)"
                                       : "Cena e Jogo lado a lado: desligado");
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
