// Scripts do projeto no editor: compilar a pasta Scripts/ (ao abrir o projeto, ao salvar um arquivo e pelo
// menu), mostrar a saída e os erros, e recarregar a DLL sem fechar o editor (adiado até parar o Play).

#include "EditorApp.h"

#include "FileDialog.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <format>

namespace editor {

using namespace motor;
namespace fs = std::filesystem;
using Diagnostic = ScriptProject::Diagnostic;

namespace {
constexpr ImVec4 kErrorColor{1.0f, 0.45f, 0.40f, 1.0f};
constexpr ImVec4 kWarningColor{1.0f, 0.80f, 0.35f, 1.0f};
constexpr ImVec4 kOkColor{0.45f, 0.85f, 0.50f, 1.0f};
constexpr float kWatchInterval = 0.75f;  // segundos entre verificações da pasta Scripts

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

ScriptHost::ForEachScene EditorApp::AllScenes() {
    return [this](const std::function<void(Scene&)>& fn) {
        fn(m_ctx.scene);
        m_history.ForEachScene(fn);
    };
}

void EditorApp::OpenProjectScripts() {
    if (!m_project) return;
    if (!m_toolchain && m_toolchainError.empty()) {
        m_toolchain = ScriptProject::LoadToolchain(ScriptProject::DefaultSdkJson(), &m_toolchainError);
        if (!m_toolchain) log::Warn("Scripts: {}", m_toolchainError);
    }

    const ScriptProject::Paths paths = ScriptProject::PathsFor(m_project->RootDir());
    m_scriptSignature = ScriptProject::Signature(paths.scriptsDir);
    m_scriptDiagnostics.clear();
    m_scriptLog.clear();

    // DLL da sessão anterior: carrega já, para a cena abrir com os scripts ligados. A compilação logo abaixo
    // atualiza se algum arquivo mudou com o editor fechado.
    std::error_code ec;
    if (fs::is_regular_file(paths.dll, ec)) {
        const ScriptHost::Result result = m_scriptHost.Load(paths.dll, paths.loadDir, AllScenes(), m_ctx.assets);
        if (result.ok) {
            m_loadedDllTime = FileTime(paths.dll);
        } else {
            log::Warn("Scripts: {}", result.error);
        }
    }
    if (fs::is_directory(paths.scriptsDir, ec)) RequestScriptBuild();
}

void EditorApp::CloseProjectScripts() {
    m_scriptBuilder.Cancel();
    m_scriptHost.Unload(AllScenes(), m_ctx.assets);
    m_scriptBuildQueued = false;
    m_scriptReloadPending = false;
    m_scriptSignature.clear();
    m_scriptDiagnostics.clear();
    m_scriptLog.clear();
    m_loadedDllTime = {};
}

void EditorApp::RequestScriptBuild() {
    if (!m_project) return;
    if (!m_toolchain) {
        m_scriptLog = m_toolchainError + "\n";
        m_scriptDiagnostics = {{Diagnostic::Kind::Error, {}, 0, 0, m_toolchainError}};
        return;
    }
    if (m_scriptBuilder.IsRunning()) {
        m_scriptBuildQueued = true;
        return;
    }
    m_scriptBuildQueued = false;
    m_scriptDiagnostics.clear();
    m_scriptBuilder.Start(ScriptProject::PathsFor(m_project->RootDir()), *m_toolchain);
    log::Info("Scripts: compilando...");
}

void EditorApp::ReloadScripts() {
    if (!m_project || m_play.IsActive()) return;
    m_scriptReloadPending = false;
    const ScriptProject::Paths paths = ScriptProject::PathsFor(m_project->RootDir());
    const ScriptHost::Result result = m_scriptHost.Load(paths.dll, paths.loadDir, AllScenes(), m_ctx.assets);
    if (!result.ok) {
        m_scriptDiagnostics.push_back({Diagnostic::Kind::Error, {}, 0, 0, result.error});
        m_focusScriptsPanel = true;
        log::Error("Scripts: {}", result.error);
        return;
    }
    m_loadedDllTime = FileTime(paths.dll);
    m_ctx.eulerEntity = kNullEntity;
}

void EditorApp::UpdateScripts(float dt) {
    if (!m_project) return;
    const ScriptProject::Paths paths = ScriptProject::PathsFor(m_project->RootDir());

    if (m_scriptBuilder.IsRunning()) m_scriptLog = m_scriptBuilder.Log();
    if (m_scriptBuilder.Poll()) {
        m_scriptLog = m_scriptBuilder.Log();
        m_scriptDiagnostics = ScriptProject::ParseDiagnostics(m_scriptLog);
        if (m_scriptBuilder.GetState() == ScriptBuilder::State::Succeeded) {
            log::Info("Scripts: compilados em {:.1f} s", m_scriptBuilder.Seconds());
            // O make não reescreve a DLL se nada mudou: aí não há o que recarregar.
            if (!m_scriptHost.IsLoaded() || FileTime(paths.dll) != m_loadedDllTime) {
                if (m_play.IsActive()) {
                    m_scriptReloadPending = true;
                    log::Info("Scripts: recompilados durante o Play; serão recarregados ao parar");
                } else {
                    ReloadScripts();
                }
            }
        } else {
            log::Error("Scripts: a compilação falhou ({} erro(s)); veja o painel Scripts", CountErrors(m_scriptDiagnostics));
            m_focusScriptsPanel = true;
        }
        if (m_scriptBuildQueued) RequestScriptBuild();
    }

    // Vigia a pasta: arquivo salvo, criado, apagado ou renomeado = compila de novo.
    m_scriptWatchTimer += dt;
    if (m_scriptWatchTimer >= kWatchInterval) {
        m_scriptWatchTimer = 0.0f;
        std::string signature = ScriptProject::Signature(paths.scriptsDir);
        if (signature != m_scriptSignature) {
            m_scriptSignature = std::move(signature);
            RequestScriptBuild();
        }
    }
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void EditorApp::DrawScriptsMenu() {
    if (!ImGui::BeginMenu("Scripts")) return;
    const bool hasProject = m_project.has_value();
    const ScriptProject::Paths paths = hasProject ? ScriptProject::PathsFor(m_project->RootDir()) : ScriptProject::Paths{};

    if (ImGui::MenuItem("Recompilar scripts", "F7", false, hasProject)) RequestScriptBuild();
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
                    ShowError(error);
                }
            }
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Abrir pasta de scripts", nullptr, false, hasProject)) {
        std::error_code ec;
        fs::create_directories(paths.scriptsDir, ec);
        Shell::RevealInExplorer(paths.scriptsDir);
    }
    if (ImGui::MenuItem("Painel Scripts")) m_focusScriptsPanel = true;
    if (!hasProject) {
        ImGui::Separator();
        ImGui::TextDisabled("Scripts pertencem a um projeto: abra ou crie um.");
    }
    ImGui::EndMenu();
}

void EditorApp::DrawScriptsStatus() {
    if (m_scriptBuilder.IsRunning()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Compilando scripts... %.0f s", m_scriptBuilder.Seconds());
    } else if (m_scriptReloadPending) {
        ImGui::Spacing();
        ImGui::TextColored(kWarningColor, "Scripts recompilados: recarregam ao parar o Play");
    } else if (const size_t errors = CountErrors(m_scriptDiagnostics); errors > 0) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
        if (ImGui::SmallButton(std::format("Scripts: {} erro(s)", errors).c_str())) m_focusScriptsPanel = true;
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("A compilação dos scripts falhou. Clique para ver os erros.");
    }
}

void EditorApp::DrawScriptsPanel() {
    if (std::exchange(m_focusScriptsPanel, false)) ImGui::SetNextWindowFocus();
    if (!ImGui::Begin("Scripts")) {
        ImGui::End();
        return;
    }
    if (!m_project) {
        ImGui::TextDisabled("Abra ou crie um projeto: os scripts ficam na pasta Scripts/ do projeto.");
        ImGui::End();
        return;
    }
    const ScriptProject::Paths paths = ScriptProject::PathsFor(m_project->RootDir());

    // Situação + ações.
    const size_t errors = CountErrors(m_scriptDiagnostics);
    if (!m_toolchain) {
        ImGui::TextColored(kErrorColor, "Sem toolchain: %s", m_toolchainError.c_str());
    } else if (m_scriptBuilder.IsRunning()) {
        ImGui::Text("Compilando... %.1f s", m_scriptBuilder.Seconds());
    } else if (m_scriptBuilder.GetState() == ScriptBuilder::State::Failed) {
        ImGui::TextColored(kErrorColor, "A compilação falhou: %zu erro(s). Os scripts carregados são os da última compilação boa.",
                           errors);
    } else if (m_scriptBuilder.GetState() == ScriptBuilder::State::Succeeded) {
        ImGui::TextColored(kOkColor, "Compilado em %.1f s", m_scriptBuilder.Seconds());
    } else {
        ImGui::TextDisabled("Ainda não compilado.");
    }
    if (m_scriptReloadPending) {
        ImGui::TextColored(kWarningColor, "Recompilado durante o Play: os scripts serão recarregados ao parar.");
    }

    ImGui::BeginDisabled(m_scriptBuilder.IsRunning());
    if (ImGui::Button("Recompilar (F7)")) RequestScriptBuild();
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
    if (!m_scriptDiagnostics.empty()) {
        ImGui::SeparatorText("Erros e avisos");
        for (size_t i = 0; i < m_scriptDiagnostics.size(); ++i) {
            const Diagnostic& d = m_scriptDiagnostics[i];
            const bool error = d.kind == Diagnostic::Kind::Error;
            std::string text = d.file.empty() ? d.message
                                              : std::format("{}({}): {}", ToUtf8(PathFromUtf8(d.file).filename()), d.line, d.message);
            ImGui::PushID(static_cast<int>(i));
            ImGui::PushStyleColor(ImGuiCol_Text, error ? kErrorColor : kWarningColor);
            if (ImGui::Selectable((std::string(error ? "erro  " : "aviso ") + text).c_str()) && !d.file.empty()) {
                Shell::OpenWithDefaultApp(PathFromUtf8(d.file));
            }
            ImGui::PopStyleColor();
            if (!d.file.empty()) ImGui::SetItemTooltip("%s:%d:%d", d.file.c_str(), d.line, d.column);
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
        const std::string label = ToGenericUtf8(file.lexically_relative(paths.scriptsDir));
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            Shell::OpenWithDefaultApp(file);
        }
        ImGui::SetItemTooltip("Duplo clique abre no editor de código");
    }
    std::string types;
    for (const std::string& name : m_scriptHost.Types()) types += (types.empty() ? "" : ", ") + name;
    ImGui::TextDisabled("Carregados: %s", m_scriptHost.IsLoaded() ? (types.empty() ? "(nenhum)" : types.c_str()) : "nenhuma DLL");

    // Saída completa do CMake/compilador.
    if (ImGui::CollapsingHeader("Saída da compilação", errors > 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        ImGui::BeginChild("##script_log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(m_scriptLog.data(), m_scriptLog.data() + m_scriptLog.size());
        if (m_scriptBuilder.IsRunning() && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    ImGui::End();
}

void EditorApp::DrawNewScriptPopup() {
    const char* title = "Novo script";
    if (m_openNewScriptPopup) {
        ImGui::OpenPopup(title);
        m_openNewScriptPopup = false;
        m_newScriptName = "MeuScript";
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const ImVec2 buttonSize{110.0f, 0.0f};
    ImGui::TextUnformatted("Nome (vira o nome da struct e do arquivo .cpp)");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20.0f);
    ImGui::InputText("##script_name", &m_newScriptName);

    std::string problem = ScriptProject::ValidateScriptName(m_newScriptName);
    const ScriptProject::Paths paths = ScriptProject::PathsFor(m_project ? m_project->RootDir() : fs::path());
    std::error_code ec;
    if (problem.empty() && fs::exists(paths.scriptsDir / (m_newScriptName + ".cpp"), ec)) problem = "Já existe um script com esse nome.";
    if (problem.empty()) {
        ImGui::TextDisabled("Scripts/%s.cpp", m_newScriptName.c_str());
    } else {
        ImGui::TextColored(kErrorColor, "%s", problem.c_str());
    }

    ImGui::BeginDisabled(!problem.empty() || !m_project);
    const bool create = ImGui::Button("Criar", buttonSize) || (problem.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter, false));
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancelar", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (create || cancel) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();

    if (create) {
        fs::path file;
        std::string error;
        if (ScriptProject::CreateScript(paths.scriptsDir, m_newScriptName, &file, &error)) {
            log::Info("Scripts: criado {}", ToUtf8(file));
            Shell::OpenWithDefaultApp(file);  // a vigia da pasta compila sozinha
        } else {
            ShowError(error);
        }
    }
}

} // namespace editor
