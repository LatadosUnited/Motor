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
    if (const auto key = ParseKeyName(name)) return *key;  // os nomes da engine: numpad5, leftbracket, f13...
    throw ToolError("tecla desconhecida: " + name + " (use letras, dígitos, space, enter, escape, tab, up, down, "
                    "left, right, shift, ctrl, alt, f1..f24 ou os nomes da engine, como numpad5)");
}

// Botão do mouse: "left", "right", "middle", "back", "forward" (ou os nomes da engine, "MouseLeft"...).
MouseButton ParseMouse(std::string name) {
    static const std::pair<const char*, MouseButton> named[] = {{"left", MouseButton::Left},
                                                                {"right", MouseButton::Right},
                                                                {"middle", MouseButton::Middle},
                                                                {"back", MouseButton::Back},
                                                                {"forward", MouseButton::Forward}};
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    for (const auto& [text, button] : named) {
        if (name == text) return button;
    }
    if (const auto button = ParseMouseButton(name)) return *button;
    throw ToolError("botão do mouse desconhecido: " + name + " (use left, right, middle, back, forward)");
}

// Quando soltar uma tecla/botão de "tap" (segurada `seconds`) ou nunca (press: até pedirem release).
std::chrono::steady_clock::time_point ReleaseTime(const Json& args, const std::string& action) {
    if (action != "tap") return std::chrono::steady_clock::time_point::max();
    const double seconds = std::clamp(args.value("seconds", 0.1), 0.0, 30.0);
    return std::chrono::steady_clock::now() +
           std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds + 0.02));
}
// Momento em que a espera de uma ferramenta com wait desiste.
std::chrono::steady_clock::time_point Deadline(double seconds) {
    return std::chrono::steady_clock::now() +
           std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
}

// Situação dos scripts como scripts_status devolve (também a resposta de scripts_build com wait).
Json ScriptStatusJson(const ScriptController::Status& s, bool withLog) {
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
    if (withLog) j["log"] = s.log;
    return j;
}
} // namespace
// --- Jogo: Play, teclas, cliques na UI, scripts --------------------------------------------------------

void McpController::AddPlayTools() {
    m_protocol.AddTool({
        .name = "play",
        .description = "Controla o modo Play: play (começa ou continua), pause, resume, step (um quadro, em "
                       "pausa) ou stop (volta à cena de antes do Play). O pedido vale no próximo quadro: com "
                       "wait = true a resposta só vem quando ele valeu (o jogo já rodou o primeiro quadro, pausou, "
                       "deu o passo ou parou), com o estado e o quadro do jogo. Sem wait, confira com editor_status.",
        .inputSchema = Schema({{"action", {{"type", "string"}, {"enum", {"play", "pause", "resume", "step", "stop"}}}},
                               {"wait", Prop("boolean", "responder só quando o pedido valer (padrão: false)")},
                               {"timeout", Prop("number", "segundos de espera com wait (padrão 30, máx. 300)")}},
                              {"action"}),
        .run = [this](const Json& args) {
            using Request = PlayController::Request;
            PlayController& play = m_host.play;
            const std::string action = args.value("action", "");
            // Quadro do jogo agora (o step confere que andou um).
            auto frame = [&play]() -> uint64_t {
                SceneRuntime* runtime = play.Runtime();
                return runtime ? runtime->Time().frame : 0;
            };
            const uint64_t frameBefore = frame();
            std::function<bool()> applied;  // o pedido já valeu?
            if (action == "play" || action == "resume") {
                play.Queue(Request::Play);
                applied = [&play, frame] { return play.IsActive() && !play.IsPaused() && frame() >= 1; };
            } else if (action == "pause") {
                if (!play.IsActive()) throw ToolError("o jogo não está rodando");
                if (!play.IsPaused()) play.Queue(Request::Pause);
                applied = [&play] { return play.IsPaused(); };
            } else if (action == "step") {
                if (!play.IsPaused()) throw ToolError("step só em pausa (use pause antes)");
                play.Queue(Request::Step);
                applied = [frame, frameBefore] { return frame() > frameBefore; };
            } else if (action == "stop") {
                play.Queue(Request::Stop);
                applied = [&play] { return !play.IsActive(); };
            } else {
                throw ToolError("ação inválida: " + action);
            }
            ToolResult now = ToolResult::Text(std::format("pedido '{}' enviado (estado agora: {})", action, PlayState(play)));
            if (!args.value("wait", false)) return now;

            const auto deadline = Deadline(std::clamp(args.value("timeout", 30.0), 0.1, 300.0));
            now.later = [this, action, applied, frame, deadline](ToolResult& out) {
                PlayController& play = m_host.play;
                if (play.Pending() == PlayController::Request::None && applied()) {
                    out = ToolResult::Data({{"action", action}, {"state", PlayState(play)}, {"frame", frame()}});
                    return true;
                }
                if (std::chrono::steady_clock::now() < deadline) return false;
                throw ToolError(std::format("o pedido '{}' não valeu a tempo (estado: {}, quadro {}); veja log_read",
                                            action, PlayState(play), frame()));
            };
            return now;
        },
    });

    m_protocol.AddTool({
        .name = "game_key",
        .description = "Aperta teclas no jogo (só durante o Play), sem mexer no teclado do sistema: o jogo recebe "
                       "como se o jogador apertasse. tap = aperta e solta depois de 'seconds'; press/release = "
                       "segura até soltar. Ex.: tap 'space' para pular, press 'w' + release 'w' para andar.",
        .inputSchema = Schema({{"key", Prop("string", "a..z, 0..9, space, enter, escape, tab, up, down, left, right, "
                                                      "shift, ctrl, alt, f1..f24, ou um nome da engine (numpad5, "
                                                      "leftbracket...)")},
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
                m_pendingKeys.emplace_back(key, true);
                m_heldKeys.push_back({key, ReleaseTime(args, action)});
            }
            m_host.play.SetExternalInput(true);
            return ToolResult::Text(std::format("tecla {}: {}", args.value("key", ""), action));
        },
    });

    m_protocol.AddTool({
        .name = "game_gamepad",
        .description = "Simula controles (gamepad, padrão Xbox) no jogo durante o Play, sem um controle de verdade: "
                       "botões (tap/press/release, como game_key) e eixos (value fica até mudar). pad escolhe o "
                       "controle (0 a 3) e count quantos estão ligados (jogo de vários jogadores; mudar o count avisa "
                       "WasGamepadConnected/Disconnected). Enquanto simulado, os de verdade são ignorados; disconnect "
                       "(ou parar o Play) volta a eles. Ex.: tap 'A' para pular, axis 'LeftX' value 1 para andar para a "
                       "direita, count 2 e pad 1 para o segundo jogador.",
        .inputSchema = Schema({{"button", Prop("string", "A, B, X, Y, LeftBumper, RightBumper, Back, Start, LeftThumb, "
                                                         "RightThumb, DpadUp, DpadRight, DpadDown, DpadLeft")},
                               {"axis", Prop("string", "LeftX, LeftY, RightX, RightY (-1 a 1, Y para cima +), "
                                                       "LeftTrigger, RightTrigger (0 a 1)")},
                               {"value", Prop("number", "valor do eixo")},
                               {"pad", Prop("integer", "controle, 0 a 3 (padrão 0)")},
                               {"count", Prop("integer", "quantos controles simulados, 1 a 4 (padrão: os de agora, "
                                                         "pelo menos pad + 1)")},
                               {"action", {{"type", "string"}, {"enum", {"tap", "press", "release", "disconnect"}}}},
                               {"seconds", Prop("number", "tap: quanto tempo segurar (padrão 0.1)")}}),
        .run = [this](const Json& args) {
            if (!m_host.play.IsActive()) throw ToolError("o jogo não está rodando (use play)");
            const std::string action = args.value("action", std::string("tap"));
            if (action == "disconnect") {
                m_heldPad.clear();
                m_pendingPad.clear();
                m_pendingAxes.clear();
                m_pendingPadCount = 0;
                return ToolResult::Text("controle simulado desligado");
            }
            const int pad = args.value("pad", 0);
            if (pad < 0 || pad >= kMaxGamepads) throw ToolError(std::format("pad vai de 0 a {}", kMaxGamepads - 1));
            // A contagem que vai valer: a pedida antes neste quadro, ou a de agora.
            const int current = m_pendingPadCount >= 0     ? m_pendingPadCount
                                : Input::IsGamepadSimulated() ? Input::GamepadCount()
                                                              : 0;
            const int count = std::clamp(args.value("count", std::max(current, pad + 1)), pad + 1, kMaxGamepads);
            if (count != current) m_pendingPadCount = count;
            std::erase_if(m_heldPad, [&](const HeldPadButton& h) { return h.pad >= count; });
            m_host.play.SetExternalInput(true);
            if (args.contains("axis")) {
                const auto axis = ParseGamepadAxis(args["axis"].get<std::string>());
                if (!axis) throw ToolError("eixo desconhecido: " + args["axis"].get<std::string>());
                const float value = action == "release" ? 0.0f : args.value("value", 1.0f);
                m_pendingAxes.push_back({*axis, value, pad});
                return ToolResult::Text(std::format("controle {}: eixo {} = {:.2f}", pad, GamepadAxisName(*axis), value));
            }
            if (!args.contains("button")) {
                if (args.contains("count")) return ToolResult::Text(std::format("{} controle(s) simulado(s)", count));
                throw ToolError("diga 'button', 'axis' ou 'count'");
            }
            const auto button = ParseGamepadButton(args["button"].get<std::string>());
            if (!button) throw ToolError("botão desconhecido: " + args["button"].get<std::string>());
            std::erase_if(m_heldPad, [&](const HeldPadButton& h) { return h.button == *button && h.pad == pad; });
            if (action == "release") {
                m_pendingPad.push_back({*button, false, pad});
            } else {
                m_pendingPad.push_back({*button, true, pad});
                m_heldPad.push_back({*button, ReleaseTime(args, action), pad});
            }
            return ToolResult::Text(std::format("controle {}: botão {}: {}", pad, GamepadButtonName(*button), action));
        },
    });

    m_protocol.AddTool({
        .name = "game_mouse",
        .description = "Mexe o mouse do jogo durante o Play, sem mexer no mouse do sistema: move (dx, dy em pixels, "
                       "Y para baixo como a tela) espalhado em 'seconds' (sem seconds, num quadro só), botões "
                       "(tap/press/release, como game_key) e a roda (scroll, em entalhes, + para cima). O jogo vê "
                       "MouseDelta, IsMouseDown e as ações ligadas ao mouse (MouseX/MouseY de Olhar, um botão de "
                       "Atirar). Ex.: move [200, 0] seconds 0.5 para virar a câmera; tap 'left' para atirar. Para "
                       "botões da interface do jogo, use ui_click.",
        .inputSchema = Schema({{"move", {{"type", "array"},
                                         {"items", {{"type", "number"}}},
                                         {"description", "[dx, dy] em pixels"}}},
                               {"button", Prop("string", "left, right, middle, back, forward")},
                               {"action", {{"type", "string"}, {"enum", {"tap", "press", "release"}}}},
                               {"scroll", Prop("number", "entalhes da roda (+ para cima)")},
                               {"seconds", Prop("number", "move: em quanto tempo (padrão: um quadro); tap: quanto "
                                                          "tempo segurar (padrão 0.1)")}}),
        .run = [this](const Json& args) {
            if (!m_host.play.IsActive()) throw ToolError("o jogo não está rodando (use play)");
            std::vector<std::string> done;
            if (args.contains("move")) {
                const Json& move = args["move"];
                if (!move.is_array() || move.size() != 2 || !move[0].is_number() || !move[1].is_number()) {
                    throw ToolError("move é [dx, dy]");
                }
                const glm::vec2 delta{move[0].get<float>(), move[1].get<float>()};
                const double seconds = args.contains("button") ? 0.0 : std::clamp(args.value("seconds", 0.0), 0.0, 30.0);
                m_mouseMoves.push_back({delta, std::chrono::steady_clock::now(), seconds});
                done.push_back(std::format("move ({:.0f}, {:.0f}) em {:.2f} s", delta.x, delta.y, seconds));
            }
            if (args.contains("button")) {
                const MouseButton button = ParseMouse(args["button"].get<std::string>());
                const std::string action = args.value("action", std::string("tap"));
                std::erase_if(m_heldMouse, [&](const HeldMouseButton& h) { return h.button == button; });
                if (action == "release") {
                    m_pendingMouse.emplace_back(button, false);
                } else {
                    m_pendingMouse.emplace_back(button, true);
                    m_heldMouse.push_back({button, ReleaseTime(args, action)});
                }
                done.push_back(std::format("botão {}: {}", MouseButtonName(button), action));
            }
            if (args.contains("scroll")) {
                const float notches = args.value("scroll", 0.0f);
                m_pendingScroll += notches;
                done.push_back(std::format("roda {:+.0f}", notches));
            }
            if (done.empty()) throw ToolError("diga 'move', 'button' ou 'scroll'");
            m_host.play.SetExternalInput(true);
            std::string text = "mouse:";
            for (const std::string& d : done) text += " " + d + ";";
            text.pop_back();
            return ToolResult::Text(text);
        },
    });

    m_protocol.AddTool({
        .name = "game_text",
        .description = "Digita texto no jogo durante o Play (Script::TextInput, campos de texto), em UTF-8 com "
                       "acentos, sem mexer no teclado do sistema. Só o texto: para Enter ou Backspace, use game_key.",
        .inputSchema = Schema({{"text", Prop("string", "o texto, ex.: \"Olá, mundo\"")}}, {"text"}),
        .run = [this](const Json& args) {
            if (!m_host.play.IsActive()) throw ToolError("o jogo não está rodando (use play)");
            const std::string text = args.value("text", std::string());
            if (text.empty()) throw ToolError("text vazio");
            m_pendingText += text;
            m_host.play.SetExternalInput(true);
            return ToolResult::Text(std::format("texto: \"{}\"", text));
        },
    });

    m_protocol.AddTool({
        .name = "ui_click",
        .description = "Clica num botão da interface do jogo durante o Play: pelo nome ou id da entidade (componente "
                       "UI: Botão) ou pelo nome de um elemento de um documento de UI (.mui). O script vê WasClicked no "
                       "quadro seguinte.",
        .inputSchema = Schema({{"entity", {{"type", {"integer", "string"}}, {"description", "botão (id ou nome)"}}},
                               {"element", Prop("string", "nome do elemento num documento de UI (em vez de entity)")}}),
        .run = [this](const Json& args) {
            SceneRuntime* runtime = m_host.play.Runtime();
            if (!runtime || m_host.play.IsPaused()) throw ToolError("o jogo precisa estar rodando (play, sem pausa)");
            UiSystem* ui = runtime->GetSystem<UiSystem>();
            if (!ui) throw ToolError("a cena não tem o sistema de UI");
            UiBox box;
            std::string name;
            if (args.contains("element")) {
                name = args["element"].get<std::string>();
                const UiElement* element = ui->FindElement(name);
                if (!element) throw ToolError("nenhum documento de UI da cena tem o elemento " + name);
                if (!ui->ScreenRect(*element, box) || box.Size().x <= 0.0f) {
                    throw ToolError(name + " não está na tela (escondido ou o jogo acabou de começar)");
                }
            } else {
                if (!args.contains("entity")) throw ToolError("diga 'entity' ou 'element'");
                const Entity& e = mcp::ResolveEntity(m_host.ctx, args);
                name = e.name;
                if (!ui->ScreenRect(e.id, box) || box.Size().x <= 0.0f) {
                    throw ToolError(e.name + " não está na tela (não é elemento de UI, está escondido ou o jogo acabou de começar)");
                }
            }
            m_clickPos = box.Center();
            m_clickScreen = ui->Pointer().screenSize;
            m_clickStage = 1;
            m_host.play.SetExternalInput(true);
            return ToolResult::Text(std::format("clique em {} ({:.0f}, {:.0f})", name, m_clickPos.x, m_clickPos.y));
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
                       "compilação roda em segundo plano: com wait = true a resposta só vem quando ela termina (e a "
                       "DLL foi recarregada), já com o resultado de scripts_status; sem wait, acompanhe com "
                       "scripts_status.",
        .inputSchema = Schema({{"wait", Prop("boolean", "responder só quando a compilação terminar (padrão: false)")},
                               {"timeout", Prop("number", "segundos de espera com wait (padrão 300, máx. 600)")}}),
        .run = [this](const Json& args) {
            if (!m_host.document.GetProject()) throw ToolError("nenhum projeto aberto");
            const ScriptController::Status before = m_host.scripts.GetStatus();
            const bool wasBuilding = m_host.scripts.IsBuilding();
            m_host.scripts.RequestBuild();
            ToolResult now = ToolResult::Text("compilação pedida; veja scripts_status");
            if (!args.value("wait", false)) return now;
            if (before.state == "sem toolchain") throw ToolError("sem compilador: " + before.toolchainError);

            // Com outra compilação em andamento, a pedida agora entra na fila: espera as duas.
            const uint64_t target = before.builds + (wasBuilding ? 2 : 1);
            const auto deadline = Deadline(std::clamp(args.value("timeout", 300.0), 1.0, 600.0));
            now.later = [this, target, deadline](ToolResult& out) {
                const ScriptController::Status s = m_host.scripts.GetStatus();
                if (s.builds >= target && !m_host.scripts.IsBuilding()) {
                    out = ToolResult::Data(ScriptStatusJson(s, false));
                    out.isError = s.state != "ok";
                    return true;
                }
                if (std::chrono::steady_clock::now() < deadline) return false;
                throw ToolError("a compilação não terminou a tempo (continua em segundo plano; veja scripts_status)");
            };
            return now;
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
            return ToolResult::Data(ScriptStatusJson(m_host.scripts.GetStatus(), args.value("log", false)));
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
