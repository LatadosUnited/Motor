#include "ExportController.h"

#include "FileDialog.h"
#include "SceneDocument.h"
#include "ScriptController.h"
#include "ScriptProject.h"

#include <motor/core/EngineAssets.h>
#include <motor/core/Log.h>
#include <motor/core/Paths.h>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <format>

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

GameExport::Result ExportController::Export(const std::string& startupScene, const fs::path& outputDir, bool setStartup) {
    GameExport::Result result;
    motor::Project* project = m_host.document.GetProject();
    if (!project) {
        result.error = "Abra um projeto para exportar.";
        return result;
    }

    // Scripts: com fontes na pasta Scripts/, a DLL precisa estar compilada e em dia.
    const ScriptProject::Paths scripts = ScriptProject::PathsFor(project->RootDir());
    fs::path scriptsDll;
    if (const auto files = ScriptProject::ListScriptFiles(scripts.scriptsDir); !files.empty()) {
        const ScriptController::Status status = m_host.scripts.GetStatus();
        if (status.state == "compilando") {
            result.error = "Os scripts estão compilando: espere terminar e exporte de novo.";
            return result;
        }
        if (status.state == "falhou") {
            result.error = "Os scripts não compilaram. Corrija os erros no painel Scripts e exporte de novo.";
            return result;
        }
        // Sem compilação nesta sessão (sem toolchain...): vale a DLL que já existe, se for mais nova que os fontes.
        std::error_code ec;
        const auto dllTime = fs::last_write_time(scripts.dll, ec);
        bool upToDate = !ec;
        for (const fs::path& file : files) {
            if (upToDate && fs::last_write_time(file, ec) > dllTime) upToDate = false;
        }
        if (status.state != "ok" && !upToDate) {
            result.error = std::format("Os scripts não estão compilados ({}). Compile pelo menu Scripts.", status.state);
            return result;
        }
        scriptsDll = scripts.dll;
    }

    GameExport::Options options;
    options.startupScene = startupScene.empty() ? DefaultStartupScene() : startupScene;
    options.outputDir = outputDir.empty() ? GameExport::DefaultOutputDir(*project) : outputDir;
    // O Player e as DLLs ficam ao lado do editor (build/bin); os shaders, na pasta da engine.
    options.playerExe = motor::ExecutableDir() / "Player.exe";
    options.runtimeDir = motor::ExecutableDir();
    options.engineAssetsDir = motor::EngineAssetsDir();
    options.scriptsDll = scriptsDll;

    m_host.ctx.assets.Database().Refresh();
    result = GameExport::Export(*project, m_host.ctx.assets.Database(), options);
    if (!result.ok) return result;

    if (m_host.document.HasUnsavedChanges()) {
        result.warnings.insert(result.warnings.begin(),
                               "A cena aberta tem alterações não salvas: o jogo usa o que está gravado no arquivo.");
    }
    if (setStartup && project->ToRelative(project->StartupScene()) != options.startupScene) {
        project->SetStartupScene(project->ToAbsolute(options.startupScene));
        std::string error;
        if (!project->Save(&error)) result.warnings.push_back(error);
        else motor::log::Info("Cena inicial do projeto: {}", options.startupScene);
    }
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
    ImGui::TextDisabled("Vão para a pasta: o executável \"%s.exe\", a engine, os scripts compilados e só os assets que "
                        "o jogo usa (todas as cenas, o que elas citam e Assets/Recursos).",
                        project->Name().c_str());

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
    ImGui::BeginDisabled(m_scene.empty() || m_outputDir.empty());
    if (ImGui::Button("Exportar", buttonSize)) {
        m_result = Export(m_scene, PathFromUtf8(m_outputDir), m_setStartup);
        m_hasResult = true;
    }
    ImGui::EndDisabled();
    if (m_hasResult && m_result.ok) {
        ImGui::SameLine();
        if (ImGui::Button("Abrir pasta", buttonSize)) Shell::RevealInExplorer(m_result.executable.parent_path());
        ImGui::SameLine();
        if (ImGui::Button("Rodar o jogo", buttonSize)) Shell::OpenWithDefaultApp(m_result.executable);
    }
    ImGui::SameLine();
    if (ImGui::Button("Fechar", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace editor
