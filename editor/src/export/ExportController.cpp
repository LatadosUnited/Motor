#include "export/ExportController.h"

#include "export/ReleaseRuntime.h"

#include "FileDialog.h"
#include "document/SceneDocument.h"
#include "scripts/ScriptController.h"
#include "scripts/ScriptProject.h"

#include <motor/core/EngineAssets.h>
#include <motor/core/Log.h>
#include <motor/core/Paths.h>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <chrono>
#include <format>
#include <thread>

namespace editor {

namespace fs = std::filesystem;
using motor::PathFromUtf8;
using motor::ToUtf8;

namespace {
constexpr const char* kPopupTitle = "Exportar jogo";
const ImVec4 kErrorColor{1.0f, 0.45f, 0.4f, 1.0f};
const ImVec4 kWarnColor{1.0f, 0.8f, 0.35f, 1.0f};
} // namespace

std::string ExportController::DefaultStartupScene() const {
    motor::Project* project = m_host.document.GetProject();
    if (!project) return {};
    if (std::string startup = project->ToRelative(project->StartupScene()); !startup.empty()) return startup;
    return project->ToRelative(m_host.document.ScenePath());
}

bool ExportController::Begin(const std::string& startupScene, const fs::path& outputDir, bool setStartup,
                             GameExport::Result& failure) {
    if (IsBusy()) {
        failure.error = "Uma exportação já está em andamento.";
        return false;
    }
    motor::Project* project = m_host.document.GetProject();
    if (!project) {
        failure.error = "Abra um projeto para exportar.";
        return false;
    }

    Pending pending;
    pending.setStartup = setStartup;
    pending.options.startupScene = startupScene.empty() ? DefaultStartupScene() : startupScene;
    pending.options.outputDir = outputDir.empty() ? GameExport::DefaultOutputDir(*project) : outputDir;
    pending.options.engineAssetsDir = motor::EngineAssetsDir();

    const ScriptProject::Paths scripts = ScriptProject::PathsFor(project->RootDir());
    const std::vector<fs::path> sources = ScriptProject::ListScriptFiles(scripts.scriptsDir);

    // Motor em Release (Player, libMotor.dll e a DLL de scripts recompilada contra ele); sem um, o do editor.
    std::string why;
    if (const auto release = ReleaseRuntime::Find(ReleaseRuntime::DefaultCandidates(), &why)) {
        pending.options.playerExe = release->dir / "Player.exe";
        pending.options.runtimeDir = release->dir;
        motor::log::Info("Exportar: Motor em Release de {}", ToUtf8(release->dir));
        if (!sources.empty()) {
            const ScriptProject::Paths paths = ReleaseRuntime::ScriptPaths(project->RootDir());
            pending.options.scriptsDll = paths.dll;
            if (!m_releaseBuilder.Start(paths, release->toolchain)) {
                failure.error = "Não foi possível começar a compilação dos scripts em Release.";
                return false;
            }
            pending.building = true;
            motor::log::Info("Exportar: compilando os scripts em Release...");
        }
    } else {
        pending.options.playerExe = motor::ExecutableDir() / "Player.exe";
        pending.options.runtimeDir = motor::ExecutableDir();
        pending.warnings.push_back(
            "O jogo foi exportado com o Motor deste editor, que não é de Release (mais lento). Para exportar em "
            "Release, use uma versão instalada pelo Hub ou compile o Motor em build-release (" + why + ").");
        if (!sources.empty()) {
            // A DLL de scripts do editor precisa estar compilada e em dia.
            const ScriptController::Status status = m_host.scripts.GetStatus();
            if (status.state == "compilando") {
                failure.error = "Os scripts estão compilando: espere terminar e exporte de novo.";
                return false;
            }
            if (status.state == "falhou") {
                failure.error = "Os scripts não compilaram. Corrija os erros no painel Scripts e exporte de novo.";
                return false;
            }
            // Sem compilação nesta sessão (sem toolchain...): vale a DLL que já existe, se for mais nova que os fontes.
            std::error_code ec;
            const auto dllTime = fs::last_write_time(scripts.dll, ec);
            bool upToDate = !ec;
            for (const fs::path& file : sources) {
                if (upToDate && fs::last_write_time(file, ec) > dllTime) upToDate = false;
            }
            if (status.state != "ok" && !upToDate) {
                failure.error = std::format("Os scripts não estão compilados ({}). Compile pelo menu Scripts.", status.state);
                return false;
            }
            pending.options.scriptsDll = scripts.dll;
        }
    }
    m_pending = std::move(pending);
    return true;
}

bool ExportController::Poll(GameExport::Result& result) {
    if (!m_pending) return false;
    if (m_pending->building) {
        if (m_releaseBuilder.IsRunning()) return false;
        m_releaseBuilder.Poll();
        m_pending->building = false;
        if (m_releaseBuilder.GetState() != ScriptBuilder::State::Succeeded) {
            result = {};
            result.error = "Os scripts não compilaram em Release:";
            int shown = 0;
            for (const ScriptProject::Diagnostic& d : ScriptProject::ParseDiagnostics(m_releaseBuilder.Log())) {
                if (d.kind != ScriptProject::Diagnostic::Kind::Error || ++shown > 5) continue;
                result.error += d.file.empty() ? std::format("\n{}", d.message)
                                               : std::format("\n{}:{}: {}", d.file, d.line, d.message);
            }
            if (shown == 0) result.error += " veja o log do editor.";
            motor::log::Error("Exportar: a compilação dos scripts em Release falhou:\n{}", m_releaseBuilder.Log());
            m_pending.reset();
            return true;
        }
        motor::log::Info("Exportar: scripts compilados em Release ({:.1f} s)", m_releaseBuilder.Seconds());
    }
    result = Finish(*m_pending);
    m_pending.reset();
    return true;
}

GameExport::Result ExportController::Finish(const Pending& pending) {
    GameExport::Result result;
    motor::Project* project = m_host.document.GetProject();
    if (!project) {
        result.error = "O projeto foi fechado durante a exportação.";
        return result;
    }
    m_host.ctx.assets.Database().Refresh();
    result = GameExport::Export(*project, m_host.ctx.assets.Database(), pending.options);
    if (!result.ok) return result;
    result.warnings.insert(result.warnings.begin(), pending.warnings.begin(), pending.warnings.end());

    if (m_host.document.HasUnsavedChanges()) {
        result.warnings.insert(result.warnings.begin(),
                               "A cena aberta tem alterações não salvas: o jogo usa o que está gravado no arquivo.");
    }
    if (pending.setStartup && project->ToRelative(project->StartupScene()) != pending.options.startupScene) {
        project->SetStartupScene(project->ToAbsolute(pending.options.startupScene));
        std::string error;
        if (!project->Save(&error)) result.warnings.push_back(error);
        else motor::log::Info("Cena inicial do projeto: {}", pending.options.startupScene);
    }
    return result;
}

GameExport::Result ExportController::Export(const std::string& startupScene, const fs::path& outputDir, bool setStartup) {
    GameExport::Result result;
    if (!Begin(startupScene, outputDir, setStartup, result)) return result;
    // Sem janela (MCP): espera a compilação dos scripts em Release aqui mesmo.
    while (!Poll(result)) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return result;
}

void ExportController::Open() {
    m_openRequest = true;
}

void ExportController::DrawPopup() {
    motor::Project* project = m_host.document.GetProject();
    if (m_openRequest) {
        m_openRequest = false;
        if (!project) {
            m_host.document.ShowError("Abra um projeto para exportar.");
            return;
        }
        ImGui::OpenPopup(kPopupTitle);
        m_scene = DefaultStartupScene();
        if (m_outputDir.empty() || !fs::exists(PathFromUtf8(m_outputDir).parent_path())) {
            m_outputDir = ToUtf8(GameExport::DefaultOutputDir(*project));
        }
        m_hasResult = false;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kPopupTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (!project) {  // projeto fechado com a janela aberta
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const float fieldWidth = ImGui::GetFontSize() * 32.0f;
    const ImVec2 buttonSize{110.0f, 0.0f};

    ImGui::TextUnformatted("Cena inicial");
    ImGui::SetNextItemWidth(fieldWidth);
    if (ImGui::BeginCombo("##export_scene", m_scene.empty() ? "(escolha uma cena)" : m_scene.c_str())) {
        for (const std::string& scene : m_host.ctx.assets.ScanFiles(motor::AssetType::Scene)) {
            if (ImGui::Selectable(scene.c_str(), scene == m_scene)) m_scene = scene;
        }
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Gravar como cena inicial do projeto", &m_setStartup);

    ImGui::TextUnformatted("Pasta de saída");
    ImGui::SetNextItemWidth(fieldWidth - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputText("##export_dir", &m_outputDir);
    ImGui::SameLine();
    if (ImGui::Button("...", ImVec2(ImGui::GetFrameHeight(), 0.0f))) {
        if (const auto dir = FileDialog::PickFolder(PathFromUtf8(m_outputDir).parent_path())) m_outputDir = ToUtf8(*dir);
    }
    ImGui::PushTextWrapPos(fieldWidth);
    ImGui::TextDisabled("Vão para a pasta: o executável \"%s.exe\", a engine e os scripts compilados em Release, e só "
                        "os assets que o jogo usa (todas as cenas, o que elas citam e Assets/Recursos).",
                        project->ExecutableName().c_str());

    if (m_hasResult) {
        ImGui::Separator();
        if (m_result.ok) {
            ImGui::Text("Exportado: %zu asset(s), %.1f MB.", m_result.assets.size(),
                        static_cast<double>(m_result.bytes) / (1024.0 * 1024.0));
            for (const std::string& w : m_result.warnings) ImGui::TextColored(kWarnColor, "%s", w.c_str());
        } else {
            ImGui::TextColored(kErrorColor, "%s", m_result.error.c_str());
        }
    }
    ImGui::PopTextWrapPos();

    ImGui::Spacing();
    if (IsBusy()) {
        if (Poll(m_result)) m_hasResult = true;
        ImGui::TextDisabled("Compilando os scripts em Release... (%.0f s)", m_releaseBuilder.Seconds());
    }
    ImGui::BeginDisabled(m_scene.empty() || m_outputDir.empty() || IsBusy());
    if (ImGui::Button("Exportar", buttonSize)) {
        m_hasResult = !Begin(m_scene, PathFromUtf8(m_outputDir), m_setStartup, m_result);
    }
    ImGui::EndDisabled();
    if (m_hasResult && m_result.ok) {
        ImGui::SameLine();
        if (ImGui::Button("Abrir pasta", buttonSize)) Shell::RevealInExplorer(m_result.executable.parent_path());
        ImGui::SameLine();
        if (ImGui::Button("Rodar o jogo", buttonSize)) Shell::OpenWithDefaultApp(m_result.executable);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(IsBusy());  // a exportação termina com a janela aberta
    if (ImGui::Button("Fechar", buttonSize) || (!IsBusy() && ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

} // namespace editor
