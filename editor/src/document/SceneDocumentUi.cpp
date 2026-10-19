// Documento do editor: título da janela, menu Arquivo e popups.

#include "document/SceneDocument.h"

#include "FileDialog.h"
#include "scripts/ScriptProject.h"

#include <imgui.h>
#include <motor/core/Version.h>
#include <imgui_stdlib.h>

#include <cstdlib>
#include <format>
#include <fstream>
#include <utility>

namespace editor {

using namespace motor;
namespace fs = std::filesystem;

namespace {
constexpr const char* kUntitled = "Sem título";
constexpr const char* kUnsavedTitle = "Alterações não salvas";
constexpr const char* kNewerVersionTitle = "Projeto de versão mais nova";
constexpr const char* kRecoveryTitle = "Recuperar alterações não salvas";
constexpr const char* kErrorTitle = "Erro";
const ImVec2 kButtonSize{110.0f, 0.0f};
} // namespace

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void SceneDocument::UpdateWindowTitle() {
    const std::string scene = m_scenePath.empty() ? kUntitled : ToUtf8(m_scenePath.filename());
    const std::string dirty = m_dirty ? "*" : "";
    if (m_project) {
        m_host.setWindowTitle(std::format("Motor Editor {} - {} - {}{}", kVersion, m_project->Name(), scene, dirty));
    } else {
        m_host.setWindowTitle(std::format("Motor Editor {} - {}{}", kVersion, scene, dirty));
    }
}

void SceneDocument::ShowError(std::string message) {
    log::Error("{}", message);
    m_errorMessage = std::move(message);
    m_openErrorPopup = true;
}

void SceneDocument::DrawFileMenu() {
    if (!ImGui::BeginMenu("Arquivo")) return;

    if (ImGui::MenuItem("Novo projeto...")) RequestFileAction(FileAction::NewProject);
    if (ImGui::MenuItem("Abrir projeto...")) RequestFileAction(FileAction::OpenProject);
    const std::vector<fs::path> recent = m_settings.recentProjects;  // cópia: abrir um projeto altera a lista
    if (ImGui::BeginMenu("Projetos recentes", !recent.empty())) {
        for (const fs::path& p : recent) {
            const std::string label = std::format("{}   ({})", ToUtf8(p.stem()), ToUtf8(p.parent_path()));
            if (ImGui::MenuItem(label.c_str())) RequestFileAction(FileAction::OpenProjectPath, p);
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Nova cena", "Ctrl+N")) RequestFileAction(FileAction::NewScene);
    if (ImGui::MenuItem("Abrir cena...", "Ctrl+O")) RequestFileAction(FileAction::OpenScene);
    if (ImGui::MenuItem("Salvar cena", "Ctrl+S")) SaveScene(false);
    if (ImGui::MenuItem("Salvar cena como...", "Ctrl+Shift+S")) SaveScene(true);
    ImGui::Separator();
    if (ImGui::MenuItem("Camadas de colisão...", nullptr, false, m_project.has_value())) m_showLayersWindow = true;
    if (ImGui::MenuItem("Configurações do jogo...", nullptr, false, m_project.has_value())) {
        m_showGameSettingsWindow = true;
    }
    if (ImGui::MenuItem("Controles...", nullptr, false, m_project.has_value())) m_showControlsWindow = true;
    if (m_host.exportGame && ImGui::MenuItem("Exportar jogo...", nullptr, false, m_project.has_value())) m_host.exportGame();
    ImGui::Separator();
    if (ImGui::MenuItem("Sair", "Alt+F4")) m_host.requestClose();  // passa por OnCloseRequested

    ImGui::EndMenu();
}

void SceneDocument::DrawPopups() {
    if (m_openUnsavedPopup) {
        ImGui::OpenPopup(kUnsavedTitle);
        m_openUnsavedPopup = false;
    }
    if (m_openNewerVersionPopup) {
        ImGui::OpenPopup(kNewerVersionTitle);
        m_openNewerVersionPopup = false;
    }
    if (m_openRecoveryPopup) {
        ImGui::OpenPopup(kRecoveryTitle);
        m_openRecoveryPopup = false;
    }
    if (m_openErrorPopup) {
        ImGui::OpenPopup(kErrorTitle);
        m_openErrorPopup = false;
    }

    DrawUnsavedPopup();
    DrawNewerVersionPopup();
    DrawRecoveryPopup();
    DrawNewProjectPopup();
    DrawErrorPopup();
    DrawCollisionLayersWindow();
    DrawGameSettingsWindow();
    DrawControlsWindow();
}

// Cena com alterações não salvas antes de abrir, criar ou sair: salvar, não salvar ou cancelar.
void SceneDocument::DrawUnsavedPopup() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(kUnsavedTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string name = m_scenePath.empty() ? kUntitled : ToUtf8(m_scenePath.filename());
        ImGui::Text("A cena \"%s\" tem alterações não salvas.", name.c_str());
        ImGui::TextUnformatted("Deseja salvar antes de continuar?");
        ImGui::Spacing();

        FileAction action = FileAction::None;
        bool close = false;
        if (ImGui::Button("Salvar", kButtonSize)) {
            close = true;
            if (SaveScene(false)) action = m_pendingAction;  // se cancelar o diálogo, não continua
        }
        ImGui::SameLine();
        if (ImGui::Button("Não salvar", kButtonSize)) {
            close = true;
            ClearRecovery();
            action = m_pendingAction;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", kButtonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) close = true;

        const fs::path path = m_pendingPath;
        if (close) {
            ImGui::CloseCurrentPopup();
            m_pendingAction = FileAction::None;
            m_pendingPath.clear();
        }
        ImGui::EndPopup();
        // Executa depois de fechar o popup: algumas ações abrem outro diálogo modal.
        if (action != FileAction::None) PerformFileAction(action, path);
    }
}

// Projeto salvo por um Motor mais novo: abrir mesmo assim ou cancelar.
void SceneDocument::DrawNewerVersionPopup() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(kNewerVersionTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::Text("O projeto \"%s\" foi salvo no Motor %s, mais novo que este editor (%s).",
                    m_pendingNewerProjectName.c_str(), m_pendingNewerProjectVersion.c_str(), kVersion);
        ImGui::Spacing();
        ImGui::TextUnformatted("Abrir aqui pode ignorar campos novos e atualizar o projeto para esta versão.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        const ImVec2 wideButton{160.0f, 0.0f};
        const bool openAnyway = ImGui::Button("Abrir mesmo assim", wideButton);
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancelar", kButtonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        const fs::path path = m_pendingNewerProjectPath;
        const bool restore = m_pendingNewerProjectRestoreScene;
        if (openAnyway || cancel) {
            ImGui::CloseCurrentPopup();
            m_pendingNewerProjectPath.clear();
        }
        ImGui::EndPopup();
        if (openAnyway) OpenProjectAt(path, restore, true);
    }
}

// Estado não salvo encontrado ao abrir o projeto: recuperar ou descartar.
void SceneDocument::DrawRecoveryPopup() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(kRecoveryTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        const std::string sceneLabel = m_pendingRecovery.originalPath.empty()
                                           ? std::string(kUntitled)
                                           : ToUtf8(m_pendingRecovery.originalPath.filename());
        const char* reasonText =
            m_pendingRecovery.reason == "play" ? "antes do Play" : "pelo salvamento automático";
        ImGui::Text("Foi encontrado um estado não salvo da cena \"%s\" (gravado %s).",
                    sceneLabel.c_str(), reasonText);
        ImGui::Spacing();
        ImGui::TextUnformatted("Deseja recuperar essas alterações?");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        const bool recover = ImGui::Button("Recuperar", kButtonSize) || ImGui::IsKeyPressed(ImGuiKey_Enter, false);
        ImGui::SameLine();
        const bool discard = ImGui::Button("Descartar", kButtonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        SceneSerializer::RecoveryInfo info;
        if (recover || discard) {
            info = std::move(m_pendingRecovery);
            m_pendingRecovery = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        if (recover) {
            ApplyRecovery(info);
        } else if (discard) {
            ClearRecovery();
        }
    }
}

// Mensagem de erro (m_errorMessage).
void SceneDocument::DrawErrorPopup() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(kErrorTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(m_errorMessage.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("OK", kButtonSize) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void SceneDocument::DrawNewProjectPopup() {
    const char* title = "Novo projeto";
    if (m_openNewProjectPopup) {
        ImGui::OpenPopup(title);
        m_openNewProjectPopup = false;
        if (m_newProjectEmptyDir.empty()) {
            m_newProjectName = "MeuJogo";
            if (m_newProjectLocation.empty()) m_newProjectLocation = ToUtf8(DefaultProjectsDir());
        }
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const float fieldWidth = ImGui::GetFontSize() * 28.0f;
    const ImVec2 buttonSize{110.0f, 0.0f};

    if (!m_newProjectEmptyDir.empty()) {
        ImGui::PushTextWrapPos(fieldWidth);
        ImGui::Text("A pasta \"%s\" está vazia. Deseja criar um projeto nela?", m_newProjectEmptyDir.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }

    ImGui::TextUnformatted("Nome");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(fieldWidth);
    ImGui::InputText("##project_name", &m_newProjectName);

    ImGui::TextUnformatted("Local");
    ImGui::SetNextItemWidth(fieldWidth - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputText("##project_location", &m_newProjectLocation);
    ImGui::SameLine();
    if (ImGui::Button("...", ImVec2(ImGui::GetFrameHeight(), 0.0f))) {
        if (const auto dir = FileDialog::PickFolder(PathFromUtf8(m_newProjectLocation))) {
            m_newProjectLocation = ToUtf8(*dir);
        }
    }

    // Validação ao vivo.
    std::error_code ec;
    const fs::path location = PathFromUtf8(m_newProjectLocation);
    const fs::path target = location / PathFromUtf8(m_newProjectName);
    std::string problem = Project::ValidateName(m_newProjectName);
    if (problem.empty() && !fs::is_directory(location, ec)) problem = "A pasta escolhida em \"Local\" não existe.";
    if (problem.empty() && fs::exists(target, ec) && !fs::is_empty(target, ec)) {
        problem = "Já existe uma pasta com esse nome nesse local.";
    }

    ImGui::Spacing();
    ImGui::PushTextWrapPos(fieldWidth);
    if (problem.empty()) {
        ImGui::TextDisabled("Será criado em: %s", ToUtf8(target).c_str());
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", problem.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    bool create = false;
    ImGui::BeginDisabled(!problem.empty());
    create = ImGui::Button("Criar", buttonSize) ||
             (problem.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter, false));
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancelar", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);

    if (create || cancel) {
        m_newProjectEmptyDir.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();

    if (create) CreateProject(location, m_newProjectName);
}

} // namespace editor
