// Operações de arquivo do editor: cenas, projetos, menu Arquivo e diálogos relacionados.

#include "EditorApp.h"

#include "FileDialog.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cstdlib>
#include <format>

namespace editor {

using namespace motor;
namespace fs = std::filesystem;

namespace {
constexpr const char* kUntitled = "Sem título";

// Pasta sugerida para novos projetos: Documentos do usuário.
fs::path DefaultProjectsDir() {
    std::error_code ec;
#ifdef _WIN32
    if (const wchar_t* profile = _wgetenv(L"USERPROFILE")) {
        const fs::path docs = fs::path(profile) / "Documents";
        if (fs::is_directory(docs, ec)) return docs;
        return profile;
    }
#else
    if (const char* home = std::getenv("HOME")) return home;
#endif
    return fs::current_path(ec);
}

// Cena inicial de um projeto novo: chão, um cubo e a câmera do jogo.
void BuildStarterScene(Scene& scene, const AssetLibrary& assets) {
    Entity& floor = scene.CreateEntity("Chão");
    floor.transform.scale = {40.0f, 1.0f, 40.0f};
    MeshRenderer& floorRenderer = floor.Add<MeshRenderer>();
    floorRenderer.mesh = assets.FindMesh("Plano");
    floorRenderer.material.albedo = assets.FindTexture("Xadrez");
    floorRenderer.material.specular = 0.1f;

    Entity& cube = scene.CreateEntity("Cubo");
    cube.transform.position = {0.0f, 0.5f, 0.0f};
    MeshRenderer& cubeRenderer = cube.Add<MeshRenderer>();
    cubeRenderer.mesh = assets.FindMesh("Cubo");
    cubeRenderer.material.color = {0.30f, 0.60f, 0.95f, 1.0f};

    Entity& camera = scene.CreateEntity("Câmera");
    camera.transform.position = {0.0f, 3.0f, 7.0f};
    camera.transform.SetEulerDegrees({-20.0f, 0.0f, 0.0f});
    camera.Add<CameraComponent>();

    scene.light.direction = {-0.5f, -1.0f, -0.35f};
}

// Primeira cena encontrada dentro de `dir` (busca recursiva), ou vazio.
fs::path FindAnyScene(const fs::path& dir) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec) && it->path().extension() == SceneSerializer::kFileExtension) return it->path();
    }
    return {};
}
} // namespace

// ---------------------------------------------------------------------------
// Ações (com confirmação de alterações não salvas)
// ---------------------------------------------------------------------------

bool EditorApp::OnCloseRequested() {
    StopPlay();  // o que importa é a cena de antes do Play
    if (!HasUnsavedChanges()) return true;
    RequestFileAction(FileAction::Quit);
    return false;
}

void EditorApp::OnShutdown() {
    m_settings.projectGridView = m_projectPanel.View().grid;
    m_settings.projectIconSize = m_projectPanel.View().iconSize;
    m_settings.Save();
}

void EditorApp::RequestFileAction(FileAction action, const fs::path& path) {
    StopPlay();  // trocar de cena ou de projeto encerra o Play (e a pergunta de salvar é sobre a cena original)
    if (HasUnsavedChanges()) {
        m_pendingAction = action;
        m_pendingPath = path;
        m_openUnsavedPopup = true;
        return;
    }
    PerformFileAction(action, path);
}

void EditorApp::PerformFileAction(FileAction action, const fs::path& path) {
    switch (action) {
        case FileAction::NewScene:
            ReplaceScene(Scene{}, {});
            log::Info("Nova cena");
            break;
        case FileAction::OpenScene:
            if (const auto chosen = FileDialog::OpenFile(FileDialog::Kind::Scene, DefaultSceneDir())) {
                OpenSceneAt(*chosen);
            }
            break;
        case FileAction::OpenScenePath:
            OpenSceneAt(path);
            break;
        case FileAction::NewProject:
            m_openNewProjectPopup = true;
            break;
        case FileAction::OpenProject: {
            const fs::path start = m_project ? m_project->RootDir().parent_path() : DefaultProjectsDir();
            if (const auto chosen = FileDialog::OpenFile(FileDialog::Kind::Project, start)) OpenProjectAt(*chosen, true);
            break;
        }
        case FileAction::OpenProjectPath:
            OpenProjectAt(path, true);
            break;
        case FileAction::Quit:
            Quit();
            break;
        case FileAction::None:
            break;
    }
}

// ---------------------------------------------------------------------------
// Cenas
// ---------------------------------------------------------------------------

fs::path EditorApp::DefaultSceneDir() const {
    if (!m_scenePath.empty()) return m_scenePath.parent_path();
    if (m_project) {
        std::error_code ec;
        const fs::path scenes = m_project->AssetsDir() / "Scenes";
        return fs::is_directory(scenes, ec) ? scenes : m_project->AssetsDir();
    }
    return {};
}

bool EditorApp::SaveScene(bool saveAs) {
    if (m_play.IsActive()) {
        // Salvar agora gravaria o estado do jogo, que é descartado ao parar.
        ShowError("Pare o Play antes de salvar (Ctrl+P). Alterações feitas durante o Play são descartadas.");
        return false;
    }
    fs::path path = m_scenePath;
    if (saveAs || path.empty()) {
        const fs::path suggestion = path.empty() ? DefaultSceneDir() / "Nova cena.mscene" : path;
        const auto chosen = FileDialog::SaveFile(FileDialog::Kind::Scene, suggestion);
        if (!chosen) return false;
        path = *chosen;
    }

    std::string error;
    if (!SceneSerializer::Save(path, m_ctx.scene, m_ctx.assets, &error)) {
        ShowError(error);
        return false;
    }
    m_scenePath = path;
    MarkSaved();

    if (m_project) {
        if (!IsInside(path, m_project->RootDir())) {
            log::Warn("A cena foi salva fora do projeto '{}'", m_project->Name());
        } else if (m_project->StartupScene().empty()) {
            // Primeira cena salva no projeto vira a cena inicial.
            m_project->SetStartupScene(path);
            m_project->Save();
        }
    }
    RememberScene();
    m_projectPanel.Refresh();
    return true;
}

bool EditorApp::OpenSceneAt(const fs::path& path) {
    Scene loaded;
    std::string error;
    if (!SceneSerializer::Load(path, loaded, m_ctx.assets, &error)) {
        ShowError(error);
        return false;
    }
    ReplaceScene(std::move(loaded), path);
    RememberScene();
    return true;
}

void EditorApp::ReplaceScene(Scene&& scene, const fs::path& path) {
    m_ctx.scene = std::move(scene);
    m_ctx.selected = kNullEntity;
    m_ctx.pendingDelete = kNullEntity;
    m_ctx.eulerEntity = kNullEntity;
    m_scenePath = path;
    m_history.Reset(m_ctx.scene, kNullEntity);  // o histórico é de uma cena só
    MarkSaved();
}

void EditorApp::MarkSaved() {
    m_savedSnapshot = SceneSerializer::ToJson(m_ctx.scene, m_ctx.assets, {.pathHints = false});
    m_dirty = false;
    UpdateWindowTitle();
}

bool EditorApp::HasUnsavedChanges() {
    const bool dirty = SceneSerializer::ToJson(m_ctx.scene, m_ctx.assets, {.pathHints = false}) != m_savedSnapshot;
    if (dirty != m_dirty) {
        m_dirty = dirty;
        UpdateWindowTitle();
    }
    return dirty;
}

void EditorApp::RememberScene() {
    if (!m_project || !IsInside(m_scenePath, m_project->RootDir())) return;
    m_settings.lastScene = m_scenePath;
    m_settings.Save();
}

bool EditorApp::CreateEmptySceneFile(const fs::path& path) {
    Scene empty;
    std::string error;
    if (!SceneSerializer::Save(path, empty, m_ctx.assets, &error)) {
        ShowError(error);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Projetos
// ---------------------------------------------------------------------------

bool EditorApp::OpenProjectAt(const fs::path& fileOrDir, bool restoreLastScene) {
    std::string error;
    auto project = Project::Open(fileOrDir, &error);
    if (!project) {
        ShowError(error);
        m_settings.RemoveRecentProject(fileOrDir);
        m_settings.Save();
        return false;
    }

    m_project = std::move(*project);
    m_settings.lastProject = m_project->ProjectFile();
    m_settings.AddRecentProject(m_project->ProjectFile());

    // Qual cena abrir: a última usada neste projeto, senão a cena inicial, senão qualquer uma.
    std::error_code ec;
    fs::path scene;
    if (restoreLastScene && IsInside(m_settings.lastScene, m_project->RootDir()) &&
        fs::is_regular_file(m_settings.lastScene, ec)) {
        scene = m_settings.lastScene;
    } else if (const fs::path startup = m_project->StartupScene(); !startup.empty() && fs::is_regular_file(startup, ec)) {
        scene = startup;
    } else {
        scene = FindAnyScene(m_project->AssetsDir());
    }

    ReplaceScene(Scene{}, {});
    m_ctx.assets.SetRoot(m_project->RootDir());  // assets de arquivo são relativos ao projeto
    if (!scene.empty()) OpenSceneAt(scene);

    m_settings.Save();
    m_projectPanel.Refresh();
    UpdateWindowTitle();
    return true;
}

void EditorApp::CreateProject(const fs::path& location, const std::string& name) {
    std::string error;
    auto project = Project::Create(location, name, &error);
    if (!project) {
        ShowError(error);
        return;
    }

    Scene starter;
    BuildStarterScene(starter, m_ctx.assets);
    const fs::path scenePath = project->AssetsDir() / "Scenes" / "Principal.mscene";
    if (SceneSerializer::Save(scenePath, starter, m_ctx.assets, &error)) {
        project->SetStartupScene(scenePath);
        project->Save(&error);
    } else {
        log::Warn("Projeto criado sem cena inicial: {}", error);
    }

    OpenProjectAt(project->ProjectFile(), false);
}

void EditorApp::OnPathMoved(const fs::path& from, const fs::path& to) {
    // O .meta foi junto: o GUID é o mesmo, só o caminho mudou. Atualiza o banco para a cena salvar o caminho novo.
    m_ctx.assets.Database().Refresh();
    m_scenePath = RebasePath(m_scenePath, from, to);
    m_settings.lastScene = RebasePath(m_settings.lastScene, from, to);

    if (m_project) {
        const fs::path startup = m_project->StartupScene();
        const fs::path moved = RebasePath(startup, from, to);
        if (moved != startup) {
            m_project->SetStartupScene(moved);
            m_project->Save();
        }
    }
    m_settings.Save();
    UpdateWindowTitle();
}

void EditorApp::OnPathDeleted(const fs::path& path) {
    m_ctx.assets.Database().Refresh();
    if (IsInside(m_scenePath, path)) {
        // A cena continua aberta na memória, mas sem arquivo: vira "Sem título" com alterações pendentes.
        m_scenePath.clear();
        m_savedSnapshot.clear();
        HasUnsavedChanges();
    }
    if (IsInside(m_settings.lastScene, path)) m_settings.lastScene.clear();
    if (m_project && IsInside(m_project->StartupScene(), path)) {
        m_project->SetStartupScene({});
        m_project->Save();
    }
    m_settings.Save();
    UpdateWindowTitle();
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void EditorApp::UpdateWindowTitle() {
    const std::string scene = m_scenePath.empty() ? kUntitled : ToUtf8(m_scenePath.filename());
    const std::string dirty = m_dirty ? "*" : "";
    if (m_project) {
        SetWindowTitle(std::format("Motor Editor - {} - {}{}", m_project->Name(), scene, dirty));
    } else {
        SetWindowTitle(std::format("Motor Editor - {}{}", scene, dirty));
    }
}

void EditorApp::ShowError(std::string message) {
    log::Error("{}", message);
    m_errorMessage = std::move(message);
    m_openErrorPopup = true;
}

void EditorApp::DrawFileMenu() {
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
    if (ImGui::MenuItem("Sair", "Alt+F4")) GetWindow().RequestClose();  // passa por OnCloseRequested

    ImGui::EndMenu();
}

void EditorApp::DrawProjectPanel() {
    ProjectPanel::Callbacks callbacks;
    callbacks.createProject = [this] { RequestFileAction(FileAction::NewProject); };
    callbacks.openProject = [this] { RequestFileAction(FileAction::OpenProject); };
    callbacks.openScene = [this](const fs::path& p) { RequestFileAction(FileAction::OpenScenePath, p); };
    callbacks.createSceneFile = [this](const fs::path& p) { return CreateEmptySceneFile(p); };
    callbacks.pathMoved = [this](const fs::path& from, const fs::path& to) { OnPathMoved(from, to); };
    callbacks.pathDeleted = [this](const fs::path& p) { OnPathDeleted(p); };
    callbacks.addMeshToScene = [this](const fs::path& p) { m_ctx.CreateFromMeshFile(p); };
    callbacks.applyTexture = [this](const fs::path& p) {
        if (m_ctx.scene.Find(m_ctx.selected)) {
            m_ctx.ApplyTextureFile(m_ctx.selected, p);
        } else {
            ShowError("Selecione uma entidade para aplicar a textura.");
        }
    };

    // Arquivos arrastados do Explorer para a janela são importados pelo painel.
    if (auto dropped = GetWindow().TakeDroppedFiles(); !dropped.empty()) m_projectPanel.QueueImport(std::move(dropped));

    m_projectPanel.Draw(m_project ? &*m_project : nullptr, m_scenePath, callbacks);
}

void EditorApp::DrawFilePopups() {
    const char* unsavedTitle = "Alterações não salvas";
    const char* errorTitle = "Erro";
    if (m_openUnsavedPopup) {
        ImGui::OpenPopup(unsavedTitle);
        m_openUnsavedPopup = false;
    }
    if (m_openErrorPopup) {
        ImGui::OpenPopup(errorTitle);
        m_openErrorPopup = false;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    const ImVec2 buttonSize{110.0f, 0.0f};

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(unsavedTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string name = m_scenePath.empty() ? kUntitled : ToUtf8(m_scenePath.filename());
        ImGui::Text("A cena \"%s\" tem alterações não salvas.", name.c_str());
        ImGui::TextUnformatted("Deseja salvar antes de continuar?");
        ImGui::Spacing();

        FileAction action = FileAction::None;
        bool close = false;
        if (ImGui::Button("Salvar", buttonSize)) {
            close = true;
            if (SaveScene(false)) action = m_pendingAction;  // se cancelar o diálogo, não continua
        }
        ImGui::SameLine();
        if (ImGui::Button("Não salvar", buttonSize)) {
            close = true;
            action = m_pendingAction;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) close = true;

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

    DrawNewProjectPopup();

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(errorTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(m_errorMessage.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("OK", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void EditorApp::DrawNewProjectPopup() {
    const char* title = "Novo projeto";
    if (m_openNewProjectPopup) {
        ImGui::OpenPopup(title);
        m_openNewProjectPopup = false;
        m_newProjectName = "MeuJogo";
        if (m_newProjectLocation.empty()) m_newProjectLocation = ToUtf8(DefaultProjectsDir());
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const float fieldWidth = ImGui::GetFontSize() * 28.0f;
    const ImVec2 buttonSize{110.0f, 0.0f};

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

    if (create || cancel) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();

    if (create) CreateProject(location, m_newProjectName);
}

} // namespace editor
