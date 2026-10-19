// Ferramentas MCP do jogo (Play, teclas, cliques na UI, áudio) e dos scripts e da exportação.

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
} // namespace
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
                                  {"gain", v.gain}, {"one_shots", v.oneShots}});
            }
            const glm::vec3 p = audio->ListenerPosition();
            return ToolResult::Data({{"device", audio->HasDevice()}, {"paused", audio->IsPaused()},
                                     {"sample_rate", audio->SampleRate()}, {"listener", {p.x, p.y, p.z}},
                                     {"output_level", {audio->OutputLevel(0), audio->OutputLevel(1)}},
                                     {"voices", std::move(voices)}});
        },
    });

    m_protocol.AddTool({
        .name = "game_saves",
        .description = "Saves dos scripts no Play do editor (Script::SaveSlot, pasta <projeto>/.motor/saves): lista os "
                       "slots com os valores (no Play, também o que ainda não foi gravado) ou apaga (um slot ou todos, "
                       "como Jogo > Apagar saves do Play).",
        .inputSchema = Schema({{"action", Prop("string", "list (padrão) ou delete")},
                               {"slot", Prop("string", "nome do slot; no delete sem slot, apaga todos")}}),
        .run = [this](const Json& args) {
            const std::filesystem::path folder = m_host.play.PlaySavesFolder();
            if (folder.empty()) throw ToolError("nenhum projeto aberto");
            const std::string action = args.value("action", std::string("list"));
            SaveStore disk(folder);
            SaveStore* live = m_host.play.PlaySaves();
            if (action == "delete") {
                if (args.contains("slot")) {
                    const std::string slot = args["slot"].get<std::string>();
                    disk.Delete(slot);
                    if (live) live->Delete(slot);
                    return ToolResult::Text("slot apagado: " + slot);
                }
                return ToolResult::Text(std::format("saves apagados: {}", m_host.play.DeletePlaySaves()));
            }
            if (action != "list") throw ToolError("action: use list ou delete");
            std::vector<std::string> names = disk.List();
            if (args.contains("slot")) names = {args["slot"].get<std::string>()};
            Json slots = Json::array();
            for (const std::string& name : names) {
                SaveData& data = live ? live->Slot(name) : disk.Slot(name);
                Json values = Json::parse(data.ToJson())["values"];
                slots.push_back({{"slot", name}, {"file", ToUtf8(data.File())}, {"unsaved", data.IsDirty()},
                                 {"values", std::move(values)}});
            }
            return ToolResult::Data({{"folder", ToUtf8(folder)}, {"slots", std::move(slots)}});
        },
    });
}

// --- Scripts e exportação do jogo -------------------------------------------------------------------

void McpController::AddScriptTools() {
    m_protocol.AddTool({
        .name = "scripts_build",
        .description = "Compila os scripts do projeto (pasta Scripts/) e recarrega sem fechar o editor (durante o "
                       "Play também: os scripts recomeçam com OnStart, mantendo os campos registrados). A "
                       "compilação roda em segundo plano: acompanhe com scripts_status.",
        .inputSchema = Schema(),
        .run = [this](const Json&) {
            if (!m_host.document.GetProject()) throw ToolError("nenhum projeto aberto");
            m_host.scripts.RequestBuild();
            return ToolResult::Text("compilação pedida; veja scripts_status");
        },
    });

    {
        std::string templates;
        Json templateIds = Json::array();
        for (const auto& t : ScriptProject::NewScriptTemplates()) {
            templates += std::format("{}{} ({})", templates.empty() ? "" : ", ", t.id, t.description);
            templateIds.push_back(t.id);
        }
        m_protocol.AddTool({
            .name = "script_create",
            .description = "Cria Scripts/<name>.cpp a partir de um modelo, como Scripts > Novo script... no editor. "
                           "A pasta é vigiada: o editor compila sozinho (acompanhe com scripts_status). Modelos: " +
                           templates + ".",
            .inputSchema = Schema({{"name", Prop("string", "nome da struct e do arquivo (letras sem acento, números e _)")},
                                   {"template", Json{{"type", "string"},
                                                     {"enum", templateIds},
                                                     {"description", "modelo (padrão: comportamento)"}}}},
                                  {"name"}),
            .run = [this](const Json& args) {
                if (!m_host.document.GetProject()) throw ToolError("nenhum projeto aberto");
                const std::string name = args.value("name", std::string());
                const std::string templateId = args.value("template", std::string(ScriptProject::kDefaultScriptTemplate));
                const auto& all = ScriptProject::NewScriptTemplates();
                if (std::none_of(all.begin(), all.end(), [&](const auto& t) { return templateId == t.id; })) {
                    throw ToolError("modelo desconhecido: " + templateId);
                }
                fs::path file;
                std::string error;
                if (!m_host.scripts.CreateScript(name, templateId, &file, &error)) throw ToolError(error);
                return ToolResult::Data({{"file", motor::ToUtf8(file)}, {"template", templateId}});
            },
        });
    }

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

} // namespace editor
