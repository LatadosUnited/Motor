// Scripts do projeto no editor: compilar a pasta Scripts/ (ao abrir o projeto, ao salvar um arquivo e pelo
// menu), mostrar a saída e os erros, e recarregar a DLL sem fechar o editor (também durante o Play).

#include "scripts/ScriptController.h"

#include "play/PlayController.h"
#include "document/SceneDocument.h"
#include "scripts/ScriptIde.h"

#include "FileDialog.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <format>
#include <utility>

namespace editor {

using namespace motor;
namespace fs = std::filesystem;
using Diagnostic = ScriptProject::Diagnostic;

namespace {
constexpr ImVec4 kErrorColor{1.0f, 0.45f, 0.40f, 1.0f};
constexpr ImVec4 kWarningColor{1.0f, 0.80f, 0.35f, 1.0f};
constexpr ImVec4 kOkColor{0.45f, 0.85f, 0.50f, 1.0f};
// Segundos entre verificações da pasta Scripts: rápido sem o aviso do sistema (pasta que ainda não existe, ou
// de rede); com ele, só de reserva.
constexpr float kWatchInterval = 0.75f;
constexpr float kWatchIntervalNotified = 3.0f;

size_t CountErrors(const std::vector<Diagnostic>& diagnostics) {
    size_t n = 0;
    for (const Diagnostic& d : diagnostics) n += d.kind == Diagnostic::Kind::Error;
    return n;
}

fs::file_time_type FileTime(const fs::path& path) {
    std::error_code ec;
    const auto time = fs::last_write_time(path, ec);
    return ec ? fs::file_time_type{} : time;
}
} // namespace

ScriptController::ScriptController(EditorContext& ctx, UndoHistory& history, SceneDocument& document,
                                   PlayController& play)
    : m_ctx(ctx), m_history(history), m_document(document), m_play(play) {}

ScriptHost::ForEachScene ScriptController::AllScenes() {
    return [this](const std::function<void(Scene&)>& fn) {
        fn(m_ctx.scene);
        m_history.ForEachScene(fn);
        m_ctx.prefabs.ForEachScene(fn);
        m_play.ForEachPlayScene(fn);  // no Play: a cena guardada e os prefabs do jogo
    };
}

void ScriptController::OpenProject() {
    if (!m_document.GetProject()) return;
    if (!m_toolchain && m_toolchainError.empty()) {
        m_toolchain = ScriptProject::LoadToolchain(ScriptProject::DefaultSdkJson(), &m_toolchainError);
        if (!m_toolchain) log::Warn("Scripts: {}", m_toolchainError);
    }

    const ScriptProject::Paths paths = ScriptProject::PathsFor(m_document.GetProject()->RootDir());
    m_signature = ScriptProject::Signature(paths.scriptsDir);
    m_watch.Start(paths.scriptsDir);
    m_diagnostics.clear();
    m_reloadWarnings.clear();
    m_log.clear();

    // VS Code configurado para os scripts (IntelliSense, compilar, depurar): arquivos gerados em .vscode/.
    if (m_toolchain) {
        std::string error;
        const fs::path editorExe = ScriptProject::ExecutableDir() / "MotorEditor.exe";
        if (ScriptIde::WriteVsCodeFiles(paths, *m_toolchain, editorExe, &error) < 0) log::Warn("Scripts: {}", error);
    }

    // DLL da sessão anterior: carrega já, para a cena abrir com os scripts ligados. A compilação logo abaixo
    // atualiza se algum arquivo mudou com o editor fechado.
    std::error_code ec;
    if (fs::is_regular_file(paths.dll, ec)) {
        const auto t0 = std::chrono::steady_clock::now();
        const ScriptHost::Result result = m_host.Load(paths.dll, paths.loadDir, AllScenes(), m_ctx.assets);
        m_reloadSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
        if (result.ok) {
            m_loadedDllTime = FileTime(paths.dll);
        } else {
            log::Warn("Scripts: {}", result.error);
        }
    }
    if (fs::is_directory(paths.scriptsDir, ec)) RequestBuild();
}

void ScriptController::CloseProject() {
    m_builder.Cancel();
    m_host.Unload(AllScenes(), m_ctx.assets);
    m_buildQueued = false;
    m_reloadWarnings.clear();
    m_reloadSeconds = 0.0f;
    m_watch.Stop();
    m_signature.clear();
    m_diagnostics.clear();
    m_log.clear();
    m_loadedDllTime = {};
}

ScriptController::Status ScriptController::GetStatus() const {
    Status s;
    if (!m_document.GetProject()) {
        s.state = "sem projeto";
    } else if (!m_toolchain) {
        s.state = "sem toolchain";
    } else if (m_builder.IsRunning()) {
        s.state = "compilando";
    } else if (m_builder.GetState() == ScriptBuilder::State::Succeeded) {
        s.state = "ok";
    } else if (m_builder.GetState() == ScriptBuilder::State::Failed) {
        s.state = "falhou";
    } else {
        s.state = "não compilado";
    }
    s.seconds = m_builder.Seconds();
    s.timings = m_builder.GetTimings();
    s.reloadSeconds = m_reloadSeconds;
    s.reloadPending = false;
    s.toolchainError = m_toolchainError;
    s.loadedTypes = m_host.Types();
    s.diagnostics = m_diagnostics;
    s.log = m_log;
    s.builds = m_builds;
    s.queued = m_buildQueued;
    return s;
}

void ScriptController::RequestBuild() {
    if (!m_document.GetProject()) return;
    if (!m_toolchain) {
        m_log = m_toolchainError + "\n";
        m_diagnostics = {{Diagnostic::Kind::Error, {}, 0, 0, m_toolchainError}};
        return;
    }
    if (m_builder.IsRunning()) {
        m_buildQueued = true;
        return;
    }
    m_buildQueued = false;
    m_diagnostics.clear();
    m_builder.Start(ScriptProject::PathsFor(m_document.GetProject()->RootDir()), *m_toolchain);
    log::Info("Scripts: compilando...");
}

void ScriptController::Reload() {
    if (!m_document.GetProject()) return;
    const ScriptProject::Paths paths = ScriptProject::PathsFor(m_document.GetProject()->RootDir());
    const auto t0 = std::chrono::steady_clock::now();
    // No Play: o jogo solta os temporizadores e os erros dos scripts antigos (código e objetos que vão sumir).
    // Os scripts novos começam não iniciados e recebem OnStart no próximo passo, com os campos do jogo.
    if (motor::SceneRuntime* runtime = m_play.Runtime()) runtime->ForgetScripts();
    const ScriptHost::Result result = m_host.Load(paths.dll, paths.loadDir, AllScenes(), m_ctx.assets);
    m_reloadSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
    if (!result.ok) {
        m_diagnostics.push_back({Diagnostic::Kind::Error, {}, 0, 0, result.error});
        m_focusPanel = true;
        log::Error("Scripts: {}", result.error);
        return;
    }
    m_reloadWarnings = result.warnings;
    for (const std::string& warning : m_reloadWarnings) m_diagnostics.push_back({Diagnostic::Kind::Warning, {}, 0, 0, warning});
    if (!m_reloadWarnings.empty()) m_focusPanel = true;
    if (m_play.IsActive()) log::Info("Scripts recarregados durante o Play: os scripts recomeçam (OnStart)");
    m_loadedDllTime = FileTime(paths.dll);
    m_ctx.ForgetAngles();
}

void ScriptController::Update(float dt) {
    if (!m_document.GetProject()) return;
    const ScriptProject::Paths paths = ScriptProject::PathsFor(m_document.GetProject()->RootDir());

    if (m_builder.IsRunning()) m_log = m_builder.Log();
    if (m_builder.Poll()) {
        m_log = m_builder.Log();
        m_diagnostics = ScriptProject::ParseDiagnostics(m_log);
        if (m_builder.GetState() == ScriptBuilder::State::Failed) {
            // O GCC falha sem explicar quando o caminho de um arquivo passa de 260 caracteres: diga a provável causa.
            if (std::string warning = ScriptProject::LongPathWarning(paths); !warning.empty()) {
                m_diagnostics.insert(m_diagnostics.begin(), {Diagnostic::Kind::Warning, {}, 0, 0, std::move(warning)});
            }
        }
        if (m_builder.GetState() == ScriptBuilder::State::Succeeded) {
            // O make não reescreve a DLL se nada mudou: aí não há o que recarregar.
            if (!m_host.IsLoaded() || FileTime(paths.dll) != m_loadedDllTime) Reload();
            const ScriptBuilder::Timings t = m_builder.GetTimings();
            log::Info("Scripts: compilados em {:.2f} s (configurar {:.2f} s, compilar {:.2f} s, ligar {:.2f} s, recarregar {:.3f} s)",
                      m_builder.Seconds(), t.configure, t.compile, t.link, m_reloadSeconds);
        } else {
            log::Error("Scripts: a compilação falhou ({} erro(s)); veja o painel Scripts", CountErrors(m_diagnostics));
            m_focusPanel = true;
        }
        ++m_builds;
        if (m_buildQueued) RequestBuild();
    }

    // Vigia a pasta: arquivo salvo, criado, apagado ou renomeado = compila de novo. O aviso do sistema chega na
    // hora; a assinatura confere se mudou algum arquivo de script (e não, por exemplo, um .txt).
    m_watchTimer += dt;
    if (!m_watch.IsActive() && m_watchTimer >= kWatchInterval) m_watch.Start(paths.scriptsDir);  // pasta criada agora
    const bool notified = m_watch.Changed();
    if (notified || m_watchTimer >= (m_watch.IsActive() ? kWatchIntervalNotified : kWatchInterval)) {
        m_watchTimer = 0.0f;
        std::string signature = ScriptProject::Signature(paths.scriptsDir);
        if (signature != m_signature) {
            m_signature = std::move(signature);
            RequestBuild();
        }
    }
}

void ScriptController::OnPlayStopped() {}

void ScriptController::OpenScript(const fs::path& file, int line, int column) {
    const fs::path root = m_document.GetProject() ? m_document.GetProject()->RootDir() : file.parent_path();
    ScriptIde::OpenFile(root, file, line, column);
}

bool ScriptController::CreateScript(const std::string& name, std::string_view templateId, fs::path* created,
                                    std::string* error) {
    if (!m_document.GetProject()) {
        if (error) *error = "Nenhum projeto aberto.";
        return false;
    }
    const ScriptProject::Paths paths = ScriptProject::PathsFor(m_document.GetProject()->RootDir());
    fs::path file;
    if (!ScriptProject::CreateScript(paths.scriptsDir, name, &file, error, templateId)) return false;
    log::Info("Scripts: criado {}", ToUtf8(file));
    if (created) *created = file;
    return true;  // a vigia da pasta compila sozinha
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void ScriptController::DrawMenu() {
    if (!ImGui::BeginMenu("Scripts")) return;
    const bool hasProject = m_document.GetProject() != nullptr;
    const ScriptProject::Paths paths = hasProject ? ScriptProject::PathsFor(m_document.GetProject()->RootDir()) : ScriptProject::Paths{};

    if (ImGui::MenuItem("Recompilar scripts", "F7", false, hasProject)) RequestBuild();
    if (ImGui::MenuItem("Novo script...", nullptr, false, hasProject)) m_openNewScriptPopup = true;
    if (ImGui::BeginMenu("Adicionar exemplo", hasProject)) {
        const auto templates = ScriptProject::ListTemplates(ScriptProject::DefaultTemplatesDir());
        if (templates.empty()) ImGui::TextDisabled("Nenhum modelo encontrado");
        for (const std::string& name : templates) {
            if (ImGui::MenuItem(name.c_str())) {
                std::string error;
                if (ScriptProject::CopyTemplate(ScriptProject::DefaultTemplatesDir(), name, paths.scriptsDir, &error)) {
                    log::Info("Scripts: exemplo '{}' adicionado em {}", name, ToUtf8(paths.scriptsDir));
                } else {
                    m_document.ShowError(error);
                }
            }
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Abrir no VS Code", nullptr, false, hasProject)) {
        if (!ScriptIde::OpenProjectFolder(m_document.GetProject()->RootDir())) {
            m_document.ShowError("VS Code não encontrado. Instale de code.visualstudio.com (com a extensão C/C++) "
                                 "ou abra os arquivos da pasta Scripts no seu editor.");
        }
    }
    if (ImGui::MenuItem("Abrir pasta de scripts", nullptr, false, hasProject)) {
        std::error_code ec;
        fs::create_directories(paths.scriptsDir, ec);
        Shell::RevealInExplorer(paths.scriptsDir);
    }
    if (ImGui::MenuItem("Painel Scripts")) m_focusPanel = true;
    if (!hasProject) {
        ImGui::Separator();
        ImGui::TextDisabled("Scripts pertencem a um projeto: abra ou crie um.");
    }
    ImGui::EndMenu();
}

void ScriptController::DrawStatus() {
    if (m_builder.IsRunning()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Compilando scripts... %.0f s", m_builder.Seconds());
    } else if (const size_t errors = CountErrors(m_diagnostics); errors > 0) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
        if (ImGui::SmallButton(std::format("Scripts: {} erro(s)", errors).c_str())) m_focusPanel = true;
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("A compilação dos scripts falhou. Clique para ver os erros.");
    }
}

void ScriptController::DrawPanel() {
    if (std::exchange(m_focusPanel, false)) ImGui::SetNextWindowFocus();
    if (!ImGui::Begin("Scripts")) {
        ImGui::End();
        return;
    }
    if (!m_document.GetProject()) {
        ImGui::TextDisabled("Abra ou crie um projeto: os scripts ficam na pasta Scripts/ do projeto.");
        ImGui::End();
        return;
    }
    const ScriptProject::Paths paths = ScriptProject::PathsFor(m_document.GetProject()->RootDir());

    // Situação + ações.
    const size_t errors = CountErrors(m_diagnostics);
    const ScriptBuilder::Timings timings = m_builder.GetTimings();
    if (!m_toolchain) {
        ImGui::TextColored(kErrorColor, "Sem toolchain: %s", m_toolchainError.c_str());
    } else if (m_builder.IsRunning()) {
        ImGui::Text("Compilando... %.1f s", m_builder.Seconds());
    } else if (m_builder.GetState() == ScriptBuilder::State::Failed) {
        ImGui::TextColored(kErrorColor, "A compilação falhou: %zu erro(s). Os scripts carregados são os da última compilação boa.",
                           errors);
        ImGui::TextDisabled("Configurar: %.2f s  |  Compilar: %.2f s  |  Ligar: %.2f s",
                            timings.configure, timings.compile, timings.link);
    } else if (m_builder.GetState() == ScriptBuilder::State::Succeeded) {
        ImGui::TextColored(kOkColor, "Compilado em %.2f s", m_builder.Seconds());
        ImGui::TextDisabled("Configurar: %.2f s  |  Compilar: %.2f s  |  Ligar: %.2f s  |  Recarregar: %.3f s",
                            timings.configure, timings.compile, timings.link, m_reloadSeconds);
    } else {
        ImGui::TextDisabled("Ainda não compilado.");
    }

    ImGui::BeginDisabled(m_builder.IsRunning());
    if (ImGui::Button("Recompilar (F7)")) RequestBuild();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Novo script...")) m_openNewScriptPopup = true;
    ImGui::SameLine();
    if (ImGui::Button("Abrir pasta")) {
        std::error_code ec;
        fs::create_directories(paths.scriptsDir, ec);
        Shell::RevealInExplorer(paths.scriptsDir);
    }

    // Erros e avisos (clique abre o arquivo).
    if (!m_diagnostics.empty()) {
        ImGui::SeparatorText("Erros e avisos");
        for (size_t i = 0; i < m_diagnostics.size(); ++i) {
            const Diagnostic& d = m_diagnostics[i];
            const bool error = d.kind == Diagnostic::Kind::Error;
            std::string text = d.file.empty() ? d.message
                                              : std::format("{}({}): {}", ToUtf8(PathFromUtf8(d.file).filename()), d.line, d.message);
            ImGui::PushID(static_cast<int>(i));
            ImGui::PushStyleColor(ImGuiCol_Text, error ? kErrorColor : kWarningColor);
            if (ImGui::Selectable((std::string(error ? "erro  " : "aviso ") + text).c_str()) && !d.file.empty()) {
                OpenScript(PathFromUtf8(d.file), d.line, d.column);
            }
            ImGui::PopStyleColor();
            if (!d.file.empty()) {
                ImGui::SetItemTooltip("%s:%d:%d\nClique para abrir nesta linha", d.file.c_str(), d.line, d.column);
            }
            ImGui::PopID();
        }
    }

    // Arquivos e tipos carregados.
    ImGui::SeparatorText("Scripts");
    const auto files = ScriptProject::ListScriptFiles(paths.scriptsDir);
    if (files.empty()) {
        ImGui::TextDisabled("Nenhum arquivo em %s. Use \"Novo script...\" ou Scripts > Adicionar exemplo.",
                            ToUtf8(paths.scriptsDir).c_str());
    }
    for (const fs::path& file : files) {
        const std::string rel = ToGenericUtf8(file.lexically_relative(paths.scriptsDir));
        std::string label = rel;
        for (const auto& ft : timings.files) {
            if (ft.file == rel) {
                label = std::format("{}  ({:.2f} s)", rel, ft.seconds);
                break;
            }
        }
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            OpenScript(file);
        }
        ImGui::SetItemTooltip("Duplo clique abre no editor de código");
    }
    std::string types;
    for (const std::string& name : m_host.Types()) types += (types.empty() ? "" : ", ") + name;
    ImGui::TextDisabled("Carregados: %s", m_host.IsLoaded() ? (types.empty() ? "(nenhum)" : types.c_str()) : "nenhuma DLL");

    // Saída completa do CMake/compilador.
    if (ImGui::CollapsingHeader("Saída da compilação", errors > 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        ImGui::BeginChild("##script_log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(m_log.data(), m_log.data() + m_log.size());
        if (m_builder.IsRunning() && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    ImGui::End();
}

void ScriptController::DrawNewScriptPopup() {
    const char* title = "Novo script";
    if (m_openNewScriptPopup) {
        ImGui::OpenPopup(title);
        m_openNewScriptPopup = false;
        m_newScriptName = "MeuScript";
        if (m_newScriptTemplate.empty()) m_newScriptTemplate = ScriptProject::kDefaultScriptTemplate;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const ImVec2 buttonSize{110.0f, 0.0f};
    ImGui::TextUnformatted("Nome (vira o nome da struct e do arquivo .cpp)");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20.0f);
    ImGui::InputText("##script_name", &m_newScriptName);

    ImGui::Spacing();
    ImGui::TextUnformatted("Modelo");
    for (const ScriptProject::NewScriptTemplate& t : ScriptProject::NewScriptTemplates()) {
        if (ImGui::RadioButton(t.label, m_newScriptTemplate == t.id)) m_newScriptTemplate = t.id;
        ImGui::SameLine(ImGui::GetFontSize() * 9.0f);
        ImGui::TextDisabled("%s", t.description);
    }
    ImGui::Spacing();

    std::string problem = ScriptProject::ValidateScriptName(m_newScriptName);
    const ScriptProject::Paths paths = ScriptProject::PathsFor(m_document.GetProject() ? m_document.GetProject()->RootDir() : fs::path());
    std::error_code ec;
    if (problem.empty() && fs::exists(paths.scriptsDir / (m_newScriptName + ".cpp"), ec)) problem = "Já existe um script com esse nome.";
    if (problem.empty()) {
        ImGui::TextDisabled("Scripts/%s.cpp", m_newScriptName.c_str());
    } else {
        ImGui::TextColored(kErrorColor, "%s", problem.c_str());
    }

    ImGui::BeginDisabled(!problem.empty() || !m_document.GetProject());
    const bool create = ImGui::Button("Criar", buttonSize) || (problem.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter, false));
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancelar", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (create || cancel) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();

    if (create) {
        fs::path file;
        std::string error;
        if (CreateScript(m_newScriptName, m_newScriptTemplate, &file, &error)) {
            OpenScript(file);
        } else {
            m_document.ShowError(error);
        }
    }
}

} // namespace editor
