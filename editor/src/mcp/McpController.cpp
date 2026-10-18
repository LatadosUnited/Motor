#include "McpController.h"

#include "../ExportController.h"
#include "../PlayController.h"
#include "../SceneDocument.h"
#include "../SceneViewport.h"
#include "../ScriptController.h"
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

// Nome de tecla -> Key: letras, dígitos, "space", "enter", "escape", "tab", setas, "shift", "ctrl", "alt", f1..f12.
Key ParseKey(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z') return Key(int(Key::A) + (name[0] - 'a'));
    if (name.size() == 1 && name[0] >= '0' && name[0] <= '9') return Key(int(Key::D0) + (name[0] - '0'));
    if (name.size() >= 2 && name[0] == 'f' && std::isdigit(static_cast<unsigned char>(name[1]))) {
        const int n = std::stoi(name.substr(1));
        if (n >= 1 && n <= 12) return Key(int(Key::F1) + n - 1);
    }
    static const std::pair<const char*, Key> named[] = {
        {"space", Key::Space}, {"espaço", Key::Space}, {"enter", Key::Enter}, {"escape", Key::Escape},
        {"esc", Key::Escape}, {"tab", Key::Tab}, {"backspace", Key::Backspace}, {"up", Key::Up},
        {"down", Key::Down}, {"left", Key::Left}, {"right", Key::Right}, {"shift", Key::LeftShift},
        {"ctrl", Key::LeftControl}, {"control", Key::LeftControl}, {"alt", Key::LeftAlt}, {"delete", Key::Delete}};
    for (const auto& [text, key] : named) {
        if (name == text) return key;
    }
    throw ToolError("tecla desconhecida: " + name + " (use letras, dígitos, space, enter, escape, tab, up, down, "
                    "left, right, shift, ctrl, alt, f1..f12)");
}

std::string PlayState(const PlayController& play) {
    if (!play.IsActive()) return "editando";
    return play.IsPaused() ? "pausado" : "jogando";
}

bool DiscardArg(const Json& args) {
    return args.value("discard_changes", false);
}
} // namespace

McpController::McpController(Host host)
    : m_host(std::move(host)), m_protocol("motor-editor", "0.13", kInstructions), m_server(m_protocol) {
    m_logSink = log::AddSink([this](log::Level level, std::string_view message) {
        std::lock_guard lock(m_logMutex);
        m_log.push_back({++m_logCount, level, std::string(message)});
        if (m_log.size() > kMaxLogLines) m_log.pop_front();
    });
    mcp::AddSceneTools(m_protocol, m_host.ctx);
    AddEditorTools();
    AddFileTools();
    AddPlayTools();
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

// --- Arquivos: cenas, assets, prefabs, modelos, materiais ------------------------------------------------

void McpController::AddFileTools() {
    const Json discardProp = Prop("boolean", "descartar as alterações não salvas da cena aberta (padrão: false)");

    m_protocol.AddTool({
        .name = "scene_save",
        .description = "Salva a cena aberta. Sem 'path', no arquivo atual; com 'path' (relativo ao projeto, ex.: "
                       "Assets/Cenas/Fase1.mscene), salva como esse arquivo.",
        .inputSchema = Schema({{"path", Prop("string", "arquivo .mscene (opcional)")}}),
        .run = [this](const Json& args) {
            const fs::path path = args.contains("path") ? ProjectPath(args["path"]) : fs::path{};
            std::string error;
            if (!m_host.document.SaveSceneTo(path, error)) throw ToolError(error);
            return ToolResult::Text("cena salva em " + ToUtf8(m_host.document.ScenePath()));
        },
    });

    m_protocol.AddTool({
        .name = "scene_open",
        .description = "Abre uma cena do projeto (para o Play se estiver jogando). Recusa se a cena aberta tiver "
                       "alterações não salvas, a menos que discard_changes seja true.",
        .inputSchema = Schema({{"path", Prop("string", "arquivo .mscene, relativo ao projeto")},
                               {"discard_changes", discardProp}},
                              {"path"}),
        .run = [this](const Json& args) {
            std::string error;
            if (!m_host.document.OpenSceneDirect(ProjectPath(args["path"]), DiscardArg(args), error)) throw ToolError(error);
            return ToolResult::Text(std::format("cena aberta: {} ({} entidades)", ToUtf8(m_host.document.ScenePath()),
                                                m_host.ctx.scene.Entities().size()));
        },
    });

    m_protocol.AddTool({
        .name = "scene_new",
        .description = "Começa uma cena nova e vazia (sem arquivo até salvar).",
        .inputSchema = Schema({{"discard_changes", discardProp}}),
        .run = [this](const Json& args) {
            std::string error;
            if (!m_host.document.NewSceneDirect(DiscardArg(args), error)) throw ToolError(error);
            return ToolResult::Text("cena nova aberta");
        },
    });

    m_protocol.AddTool({
        .name = "project_create",
        .description = "Cria um projeto novo, como Arquivo > Novo projeto: pasta <parent>/<name> com Assets/, Scripts/ "
                       "(script de exemplo) e a cena inicial, e abre no editor (fecha o projeto aberto). Recusa se a "
                       "cena aberta tiver alterações não salvas, a menos que discard_changes seja true.",
        .inputSchema = Schema({{"parent", Prop("string", "pasta onde o projeto será criado (caminho absoluto)")},
                               {"name", Prop("string", "nome do projeto (também o nome da pasta)")},
                               {"discard_changes", discardProp}},
                              {"parent", "name"}),
        .run = [this](const Json& args) {
            const fs::path parent = PathFromUtf8(args["parent"].get<std::string>());
            if (!parent.is_absolute()) throw ToolError("'parent' precisa ser um caminho absoluto");
            m_host.document.StopPlay();
            if (!DiscardArg(args) && m_host.document.HasUnsavedChanges()) {
                throw ToolError("a cena aberta tem alterações não salvas (salve com scene_save ou use discard_changes)");
            }
            std::string error;
            if (!m_host.document.CreateProjectAt(parent, args["name"].get<std::string>(), error)) throw ToolError(error);
            const Project* project = m_host.document.GetProject();
            return ToolResult::Text(std::format("projeto criado e aberto: {} (cena {})", ToUtf8(project->RootDir()),
                                                ToUtf8(m_host.document.ScenePath())));
        },
    });

    m_protocol.AddTool({
        .name = "assets_list",
        .description = "Arquivos da pasta Assets do projeto com o tipo (Cena, Textura, Malha, Material, Prefab, "
                       "Modelo, Fonte...). Caminhos relativos ao projeto, como as outras ferramentas aceitam.",
        .inputSchema = Schema({{"folder", Prop("string", "subpasta, relativa ao projeto (padrão: Assets)")},
                               {"type", Prop("string", "só deste tipo, ex.: Prefab")}}),
        .readOnly = true,
        .run = [this](const Json& args) {
            const Project* project = m_host.document.GetProject();
            if (!project) throw ToolError("nenhum projeto aberto");
            const fs::path root = project->RootDir();
            const fs::path folder = args.contains("folder") ? ProjectPath(args["folder"]) : root / "Assets";
            const std::string wantedType = args.value("type", std::string());
            Json files = Json::array();
            std::error_code ec;
            for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator();
                 it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                const fs::path& p = it->path();
                const std::string ext = ToUtf8(p.extension());
                if (ext == ".meta" || ext == ".tmp") continue;
                const char* type = AssetTypeName(AssetTypeFromPath(p));
                if (!wantedType.empty() && wantedType != type) continue;
                std::string relative = ToUtf8(p.lexically_relative(root));
                std::replace(relative.begin(), relative.end(), '\\', '/');
                files.push_back({{"path", std::move(relative)}, {"type", type}});
                if (files.size() >= 1000) break;
            }
            if (ec) throw ToolError("pasta não encontrada: " + ToUtf8(folder));
            return ToolResult::Data({{"files", std::move(files)}});
        },
    });

    const Json entityProp = {{"type", {"integer", "string"}}, {"description", "id ou nome da entidade"}};

    m_protocol.AddTool({
        .name = "prefab_instantiate",
        .description = "Cria na cena uma instância de um prefab (.mprefab) ou de um modelo glTF (.gltf/.glb; "
                       "importado se preciso). Fica selecionada. Devolve a raiz criada.",
        .inputSchema = Schema({{"path", Prop("string", "arquivo, relativo ao projeto")}, {"parent", entityProp}},
                              {"path"}),
        .run = [this](const Json& args) {
            EditorContext& ctx = m_host.ctx;
            const fs::path path = ProjectPath(args["path"]);
            const EntityId parent = args.contains("parent") ? mcp::ResolveEntity(ctx, args, "parent").id : kNullEntity;
            const AssetType type = AssetTypeFromPath(path);
            Entity* e = type == AssetType::Model    ? ctx.InstantiateModelFile(path, parent)
                        : type == AssetType::Prefab ? ctx.InstantiatePrefabFile(path, parent)
                                                    : nullptr;
            mcp::ThrowPendingError(ctx);
            if (!e) throw ToolError("não é um prefab nem um modelo glTF: " + ToUtf8(path));
            return ToolResult::Data(mcp::EntityToJson(ctx, *e));
        },
    });

    m_protocol.AddTool({
        .name = "prefab_create",
        .description = "Grava uma entidade (com os filhos) como prefab novo em Assets/Prefabs (ou em 'folder'); a "
                       "entidade vira uma instância dele.",
        .inputSchema = Schema({{"entity", entityProp}, {"folder", Prop("string", "pasta, relativa ao projeto")}},
                              {"entity"}),
        .run = [this](const Json& args) {
            EditorContext& ctx = m_host.ctx;
            const Entity& e = mcp::ResolveEntity(ctx, args);
            const fs::path folder = args.contains("folder") ? ProjectPath(args["folder"]) : fs::path{};
            const bool ok = ctx.CreatePrefabFromEntity(e.id, folder);
            mcp::ThrowPendingError(ctx);
            if (!ok) throw ToolError("não foi possível criar o prefab");
            return ToolResult::Text("prefab criado a partir de " + e.name);
        },
    });

    m_protocol.AddTool({
        .name = "asset_apply",
        .description = "Aplica um arquivo do projeto ao Mesh Renderer de uma entidade: material (.mmat, todos os "
                       "objetos com ele mudam juntos) ou textura (.png/.jpg).",
        .inputSchema = Schema({{"entity", entityProp}, {"path", Prop("string", "arquivo, relativo ao projeto")}},
                              {"entity", "path"}),
        .run = [this](const Json& args) {
            EditorContext& ctx = m_host.ctx;
            Entity& e = mcp::ResolveEntity(ctx, args);
            const fs::path path = ProjectPath(args["path"]);
            const AssetType type = AssetTypeFromPath(path);
            bool ok = false;
            if (type == AssetType::Material) {
                ok = ctx.ApplyMaterialFile(e.id, path);
            } else if (type == AssetType::Texture) {
                ok = ctx.ApplyTextureFile(e.id, path);
            } else {
                throw ToolError("só material (.mmat) ou textura: " + ToUtf8(path));
            }
            mcp::ThrowPendingError(ctx);
            if (!ok) throw ToolError("não foi possível aplicar (a entidade tem Mesh Renderer?)");
            return ToolResult::Data(mcp::EntityToJson(ctx, e));
        },
    });
}

// --- Jogo: Play, teclas, cliques na UI, scripts --------------------------------------------------------

void McpController::AddPlayTools() {
    m_protocol.AddTool({
        .name = "play",
        .description = "Controla o modo Play: play (começa ou continua), pause, resume, step (um quadro, em "
                       "pausa) ou stop (volta à cena de antes do Play). Aplicado no próximo quadro; confira com "
                       "editor_status.",
        .inputSchema = Schema({{"action", {{"type", "string"}, {"enum", {"play", "pause", "resume", "step", "stop"}}}}},
                              {"action"}),
        .run = [this](const Json& args) {
            using Request = PlayController::Request;
            PlayController& play = m_host.play;
            const std::string action = args.value("action", "");
            if (action == "play" || action == "resume") {
                play.Queue(Request::Play);
            } else if (action == "pause") {
                if (!play.IsActive()) throw ToolError("o jogo não está rodando");
                if (!play.IsPaused()) play.Queue(Request::Pause);
            } else if (action == "step") {
                if (!play.IsPaused()) throw ToolError("step só em pausa (use pause antes)");
                play.Queue(Request::Step);
            } else if (action == "stop") {
                play.Queue(Request::Stop);
            } else {
                throw ToolError("ação inválida: " + action);
            }
            return ToolResult::Text(std::format("pedido '{}' enviado (estado agora: {})", action, PlayState(play)));
        },
    });

    m_protocol.AddTool({
        .name = "game_key",
        .description = "Aperta teclas no jogo (só durante o Play), sem mexer no teclado do sistema: o jogo recebe "
                       "como se o jogador apertasse. tap = aperta e solta depois de 'seconds'; press/release = "
                       "segura até soltar. Ex.: tap 'space' para pular, press 'w' + release 'w' para andar.",
        .inputSchema = Schema({{"key", Prop("string", "a..z, 0..9, space, enter, escape, tab, up, down, left, right, "
                                                      "shift, ctrl, alt, f1..f12")},
                               {"action", {{"type", "string"}, {"enum", {"tap", "press", "release"}}}},
                               {"seconds", Prop("number", "tap: quanto tempo segurar (padrão 0.1)")}},
                              {"key"}),
        .run = [this](const Json& args) {
            if (!m_host.play.IsActive()) throw ToolError("o jogo não está rodando (use play)");
            const Key key = ParseKey(args.value("key", ""));
            const std::string action = args.value("action", std::string("tap"));
            std::erase_if(m_heldKeys, [&](const HeldKey& h) { return h.key == key; });
            if (action == "release") {
                m_pendingKeys.emplace_back(key, false);
            } else {
                const double seconds = action == "tap" ? std::clamp(args.value("seconds", 0.1), 0.0, 30.0) : -1.0;
                const auto releaseAt = seconds < 0 ? std::chrono::steady_clock::time_point::max()
                                                   : std::chrono::steady_clock::now() +
                                                         std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                             std::chrono::duration<double>(seconds + 0.02));
                m_pendingKeys.emplace_back(key, true);
                m_heldKeys.push_back({key, releaseAt});
            }
            m_host.play.SetExternalInput(true);
            return ToolResult::Text(std::format("tecla {}: {}", args.value("key", ""), action));
        },
    });

    m_protocol.AddTool({
        .name = "ui_click",
        .description = "Clica num botão da interface do jogo (componente UI: Botão) durante o Play, pelo nome ou "
                       "id da entidade. O script vê WasClicked no quadro seguinte.",
        .inputSchema = Schema({{"entity", {{"type", {"integer", "string"}}, {"description", "botão (id ou nome)"}}}},
                              {"entity"}),
        .run = [this](const Json& args) {
            SceneRuntime* runtime = m_host.play.Runtime();
            if (!runtime || m_host.play.IsPaused()) throw ToolError("o jogo precisa estar rodando (play, sem pausa)");
            UiSystem* ui = runtime->GetSystem<UiSystem>();
            if (!ui) throw ToolError("a cena não tem o sistema de UI");
            const Entity& e = mcp::ResolveEntity(m_host.ctx, args);
            UiBox box;
            if (!ui->ScreenRect(e.id, box) || box.Size().x <= 0.0f) {
                throw ToolError(e.name + " não está na tela (não é elemento de UI, está escondido ou o jogo acabou de começar)");
            }
            m_clickPos = box.Center();
            m_clickScreen = ui->Pointer().screenSize;
            m_clickStage = 1;
            m_host.play.SetExternalInput(true);
            return ToolResult::Text(std::format("clique em {} ({:.0f}, {:.0f})", e.name, m_clickPos.x, m_clickPos.y));
        },
    });

    m_protocol.AddTool({
        .name = "audio_status",
        .description = "Áudio durante o Play: se há saída de som, posição do ouvinte, nível do som que sai (RMS por "
                       "canal, esquerdo e direito) e as fontes que já tocaram (tocando, distância até o ouvinte e "
                       "volume com a queda da distância).",
        .inputSchema = Schema(),
        .readOnly = true,
        .run = [this](const Json&) {
            SceneRuntime* runtime = m_host.play.Runtime();
            AudioSystem* audio = runtime ? runtime->GetSystem<AudioSystem>() : nullptr;
            if (!audio) throw ToolError("o jogo não está rodando (play)");
            Json voices = Json::array();
            for (const AudioVoiceInfo& v : audio->Voices()) {
                const Entity* e = runtime->GetScene().Find(v.entity);
                voices.push_back({{"entity", v.entity}, {"name", e ? e->name : std::string()}, {"clip", v.clip},
                                  {"playing", v.playing}, {"spatial", v.spatial}, {"distance", v.distance},
                                  {"gain", v.gain}});
            }
            const glm::vec3 p = audio->ListenerPosition();
            return ToolResult::Data({{"device", audio->HasDevice()}, {"paused", audio->IsPaused()},
                                     {"sample_rate", audio->SampleRate()}, {"listener", {p.x, p.y, p.z}},
                                     {"output_level", {audio->OutputLevel(0), audio->OutputLevel(1)}},
                                     {"voices", std::move(voices)}});
        },
    });

    m_protocol.AddTool({
        .name = "scripts_build",
        .description = "Compila os scripts do projeto (pasta Scripts/) e recarrega sem fechar o editor (durante o "
                       "Play, a recarga espera parar). A compilação roda em segundo plano: acompanhe com "
                       "scripts_status.",
        .inputSchema = Schema(),
        .run = [this](const Json&) {
            if (!m_host.document.GetProject()) throw ToolError("nenhum projeto aberto");
            m_host.scripts.RequestBuild();
            return ToolResult::Text("compilação pedida; veja scripts_status");
        },
    });

    m_protocol.AddTool({
        .name = "scripts_status",
        .description = "Situação dos scripts: compilando/ok/falhou, tempos por etapa (configurar, compilar cada "
                       "arquivo, ligar, recarregar), tipos carregados e erros do compilador (arquivo, linha, "
                       "mensagem). Com log = true, inclui a saída completa da compilação.",
        .inputSchema = Schema({{"log", Prop("boolean", "incluir a saída completa (padrão: false)")}}),
        .readOnly = true,
        .run = [this](const Json& args) {
            const ScriptController::Status s = m_host.scripts.GetStatus();
            Json diagnostics = Json::array();
            for (const auto& d : s.diagnostics) {
                diagnostics.push_back({{"kind", d.kind == ScriptProject::Diagnostic::Kind::Error ? "erro" : "aviso"},
                                       {"file", d.file}, {"line", d.line}, {"column", d.column}, {"message", d.message}});
            }
            Json files = Json::array();
            for (const auto& ft : s.timings.files) {
                files.push_back({{"file", ft.file}, {"seconds", ft.seconds}});
            }
            Json timings = {{"configure", s.timings.configure},
                            {"compile", s.timings.compile},
                            {"link", s.timings.link},
                            {"reload", s.reloadSeconds},
                            {"total", s.seconds},
                            {"files", std::move(files)}};
            Json j = {{"state", s.state}, {"seconds", s.seconds}, {"timings", std::move(timings)},
                      {"loaded", s.loadedTypes}, {"reload_pending", s.reloadPending},
                      {"diagnostics", std::move(diagnostics)}};
            if (!s.toolchainError.empty()) j["toolchain_error"] = s.toolchainError;
            if (args.value("log", false)) j["log"] = s.log;
            return ToolResult::Data(j);
        },
    });

    m_protocol.AddTool({
        .name = "game_export",
        .description = "Exporta o jogo (Arquivo > Exportar jogo): uma pasta com <Nome>.exe (o Player), a engine, os "
                       "scripts compilados e só os assets usados. Os scripts precisam estar compilados (scripts_build "
                       "e scripts_status). Para testar o resultado, rode o .exe com --quadros N (fecha sozinho) e "
                       "leia o <Nome>.log ao lado dele.",
        .inputSchema = Schema({{"scene", Prop("string", "cena inicial, relativa ao projeto (padrão: a do projeto, "
                                                        "senão a cena aberta)")},
                               {"output_dir", Prop("string", "pasta de saída: absoluta ou relativa ao projeto "
                                                             "(padrão: Exportado). Precisa estar vazia ou ser uma "
                                                             "exportação anterior")},
                               {"set_startup", Prop("boolean", "gravar a cena como inicial no projeto (padrão: true)")}}),
        .run = [this](const Json& args) {
            const Project* project = m_host.document.GetProject();
            if (!project) throw ToolError("nenhum projeto aberto");
            fs::path out;
            if (args.contains("output_dir")) {
                out = PathFromUtf8(args["output_dir"].get<std::string>());
                if (out.is_relative()) out = project->RootDir() / out;
            }
            const GameExport::Result r =
                m_host.exporter.Export(args.value("scene", std::string()), out, args.value("set_startup", true));
            if (!r.ok) throw ToolError(r.error);
            return ToolResult::Data({{"executable", ToUtf8(r.executable)},
                                     {"folder", ToUtf8(r.executable.parent_path())},
                                     {"assets", r.assets},
                                     {"warnings", r.warnings},
                                     {"megabytes", static_cast<double>(r.bytes) / (1024.0 * 1024.0)}});
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
