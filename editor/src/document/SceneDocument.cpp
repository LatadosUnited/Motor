// Operações de arquivo do editor: cenas, projetos, menu Arquivo e diálogos relacionados.

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

// Cena inicial de um projeto novo: chão, um cubo e a câmera do jogo. `withScript`: o cubo gira com o script
// de exemplo Scripts/Girar.cpp.
void BuildStarterScene(Scene& scene, const AssetLibrary& assets, bool withScript) {
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
    if (withScript) {
        // O script ainda não foi compilado: entra como componente desconhecido (só o JSON) e é ligado ao
        // tipo Girar quando a DLL de scripts do projeto carregar.
        ComponentPtr girar(ComponentRegistry::MissingType());
        auto& missing = *static_cast<MissingComponent*>(girar.Data());
        missing.type = "Girar";
        missing.json = R"({"type": "Girar", "velocidade": 45.0})";
        cube.components.push_back(std::move(girar));
    }

    Entity& camera = scene.CreateEntity("Câmera");
    camera.transform.position = {0.0f, 3.0f, 7.0f};
    camera.transform.SetEulerDegrees({-20.0f, 0.0f, 0.0f});
    camera.Add<CameraComponent>();

    Entity& sun = scene.CreateEntity("Sol");
    sun.transform.position = {-2.0f, 3.0f, -1.0f};
    sun.transform.rotation = RotationLookingAt({-0.5f, -1.0f, -0.35f});
    sun.Add<LightComponent>().type = LightType::Directional;
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

SceneDocument::SceneDocument(EditorContext& ctx, UndoHistory& history, EditorSettings& settings,
                             ProjectPanel& projectPanel, Host host)
    : m_ctx(ctx), m_history(history), m_settings(settings), m_projectPanel(projectPanel), m_host(std::move(host)) {}

// ---------------------------------------------------------------------------
// Ações (com confirmação de alterações não salvas)
// ---------------------------------------------------------------------------

void SceneDocument::RequestFileAction(FileAction action, const fs::path& path) {
    m_host.stopPlay();  // trocar de cena ou de projeto encerra o Play (e a pergunta de salvar é sobre a cena original)
    if (HasUnsavedChanges()) {
        m_pendingAction = action;
        m_pendingPath = path;
        m_openUnsavedPopup = true;
        return;
    }
    PerformFileAction(action, path);
}

void SceneDocument::PerformFileAction(FileAction action, const fs::path& path) {
    switch (action) {
        case FileAction::NewScene:
            ClearRecovery();
            ReplaceScene(Scene{}, {});
            log::Info("Nova cena");
            break;
        case FileAction::OpenScene:
            if (const auto chosen = FileDialog::OpenFile(FileDialog::Kind::Scene, DefaultSceneDir())) {
                if (OpenSceneAt(*chosen)) ClearRecovery();
            }
            break;
        case FileAction::OpenScenePath:
            if (OpenSceneAt(path)) ClearRecovery();
            break;
        case FileAction::NewProject:
            m_newProjectEmptyDir.clear();
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
            m_host.quit();
            break;
        case FileAction::None:
            break;
    }
}

// ---------------------------------------------------------------------------
// Cenas
// ---------------------------------------------------------------------------

fs::path SceneDocument::DefaultSceneDir() const {
    if (!m_scenePath.empty()) return m_scenePath.parent_path();
    if (m_project) {
        std::error_code ec;
        const fs::path scenes = m_project->AssetsDir() / "Scenes";
        return fs::is_directory(scenes, ec) ? scenes : m_project->AssetsDir();
    }
    return {};
}

bool SceneDocument::SaveScene(bool saveAs) {
    if (m_host.isPlaying()) {
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
    ClearRecovery();

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

bool SceneDocument::OpenSceneDirect(const fs::path& path, bool discardChanges, std::string& error) {
    m_host.stopPlay();
    if (!discardChanges && HasUnsavedChanges()) {
        error = "a cena aberta tem alterações não salvas: salve antes ou peça para descartá-las";
        return false;
    }
    Scene loaded;
    if (!SceneSerializer::Load(path, loaded, m_ctx.assets, &error)) return false;
    ClearRecovery();
    ReplaceScene(std::move(loaded), path);
    RememberScene();
    return true;
}

bool SceneDocument::NewSceneDirect(bool discardChanges, std::string& error) {
    m_host.stopPlay();
    if (!discardChanges && HasUnsavedChanges()) {
        error = "a cena aberta tem alterações não salvas: salve antes ou peça para descartá-las";
        return false;
    }
    ClearRecovery();
    ReplaceScene(Scene{}, {});
    log::Info("Nova cena");
    return true;
}

bool SceneDocument::SaveSceneTo(const fs::path& path, std::string& error) {
    if (m_host.isPlaying()) {
        error = "pare o Play antes de salvar (as alterações do Play são descartadas)";
        return false;
    }
    const fs::path target = path.empty() ? m_scenePath : path;
    if (target.empty()) {
        error = "a cena ainda não tem arquivo: informe o caminho (ex.: Assets/Cenas/Fase1.mscene)";
        return false;
    }
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);  // ex.: Assets/Cenas ainda não existe no projeto
    if (!SceneSerializer::Save(target, m_ctx.scene, m_ctx.assets, &error)) return false;
    m_scenePath = target;
    MarkSaved();
    ClearRecovery();
    if (m_project && IsInside(target, m_project->RootDir()) && m_project->StartupScene().empty()) {
        m_project->SetStartupScene(target);
        m_project->Save();
    }
    RememberScene();
    m_projectPanel.Refresh();
    return true;
}

bool SceneDocument::OpenSceneAt(const fs::path& path) {
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

void SceneDocument::ReplaceScene(Scene&& scene, const fs::path& path) {
    m_ctx.scene = std::move(scene);
    m_ctx.selected = kNullEntity;
    m_ctx.pendingDelete = kNullEntity;
    m_ctx.eulerEntity = kNullEntity;
    m_scenePath = path;
    m_history.Reset(m_ctx.scene, kNullEntity);  // o histórico é de uma cena só
    MarkSaved();
}

void SceneDocument::MarkSaved() {
    m_savedSnapshot = SceneSerializer::ToJson(m_ctx.scene, m_ctx.assets, {.pathHints = false});
    m_dirty = false;
    m_autoSaveTimer = 0.0f;
    UpdateWindowTitle();
}

bool SceneDocument::HasUnsavedChanges() {
    const bool dirty = SceneSerializer::ToJson(m_ctx.scene, m_ctx.assets, {.pathHints = false}) != m_savedSnapshot;
    if (dirty != m_dirty) {
        m_dirty = dirty;
        if (!m_dirty) {
            m_autoSaveTimer = 0.0f;
            if (!m_host.isPlaying()) ClearRecovery();
        }
        UpdateWindowTitle();
    }
    return dirty;
}

void SceneDocument::PollChanges(float dt) {
    // Compara o JSON da cena com o salvo a cada meio segundo (cenas pequenas, custo baixo).
    // No Play a cena é a do jogo (descartada ao parar): não conta.
    if (m_host.isPlaying()) return;
    m_dirtyCheckTimer += dt;
    if (m_dirtyCheckTimer >= 0.5f) {
        m_dirtyCheckTimer = 0.0f;
        HasUnsavedChanges();
    }
    if (m_dirty) {
        m_autoSaveTimer += dt;
        constexpr float kAutoSaveIntervalSeconds = 120.0f;
        if (m_autoSaveTimer >= kAutoSaveIntervalSeconds) {
            m_autoSaveTimer = 0.0f;
            SaveRecovery("autosave");
        }
    }
}

void SceneDocument::SaveRecovery(std::string_view reason, const Scene* overrideScene) {
    if (!m_project) return;
    const Scene& scene = overrideScene ? *overrideScene : m_ctx.scene;
    std::string error;
    if (!SceneSerializer::SaveRecovery(m_project->RootDir(), scene, m_ctx.assets, m_scenePath, reason, &error)) {
        log::Warn("Não foi possível gravar a recuperação da cena: {}", error);
    }
}

void SceneDocument::ClearRecovery() {
    if (!m_project) return;
    SceneSerializer::ClearRecovery(m_project->RootDir());
}

void SceneDocument::RememberScene() {
    if (!m_project || !IsInside(m_scenePath, m_project->RootDir())) return;
    m_settings.lastScene = m_scenePath;
    m_settings.Save();
}

bool SceneDocument::CreateEmptySceneFile(const fs::path& path) {
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

bool SceneDocument::OpenProjectAt(const fs::path& fileOrDir, bool restoreLastScene, bool allowNewerVersion) {
    std::error_code ec;
    if (fs::is_directory(fileOrDir, ec) && fs::is_empty(fileOrDir, ec)) {
        OfferNewProjectInEmptyDir(fileOrDir);
        return false;
    }

    std::string error;
    auto project = Project::Open(fileOrDir, &error);
    if (!project) {
        ShowError(error);
        m_settings.RemoveRecentProject(fileOrDir);
        m_settings.Save();
        return false;
    }

    // Projeto de versão mais nova que a deste editor: avisa antes e não rebaixa o .mproject sem confirmação.
    if (project->IsNewerThanCurrentEngine() && !allowNewerVersion) {
        m_pendingNewerProjectPath = project->ProjectFile();
        m_pendingNewerProjectRestoreScene = restoreLastScene;
        m_pendingNewerProjectName = project->Name();
        m_pendingNewerProjectVersion = project->EngineVersion();
        m_openNewerVersionPopup = true;
        log::Warn("Projeto '{}' foi salvo na versão {} do Motor (atual: {}): aguardando confirmação",
                  project->Name(), project->EngineVersion(), kVersion);
        return false;
    }

    // Projeto de outra versão do Motor (ou de antes da Fase 14): passa a ser desta versão.
    if (project->EngineVersion() != kVersion) {
        const std::string from = project->EngineVersion().empty() ? "desconhecida" : project->EngineVersion();
        std::string saveError;
        if (project->Save(&saveError)) {
            log::Warn("Projeto da versão {} do Motor aberto na versão {}: engineVersion atualizado", from, kVersion);
        } else {
            log::Warn("Projeto da versão {} do Motor: não foi possível atualizar engineVersion ({})", from, saveError);
        }
    }

    if (m_project) {
        ClearRecovery();
        m_host.projectClosing();
    }
    m_project = std::move(*project);
    m_settings.lastProject = m_project->ProjectFile();
    m_settings.AddRecentProject(m_project->ProjectFile());

    const auto recovery = SceneSerializer::CheckRecovery(m_project->RootDir());

    // Qual cena abrir: a última usada neste projeto, senão a cena inicial, senão qualquer uma.
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
    m_host.projectOpened();  // antes da cena: com a DLL de scripts já carregada, os componentes abrem ligados
    if (!scene.empty()) OpenSceneAt(scene);

    if (recovery) {
        m_pendingRecovery = *recovery;
        m_openRecoveryPopup = true;
        log::Warn("Recuperação encontrada para o projeto '{}' ({})", m_project->Name(), recovery->reason);
    }

    m_settings.Save();
    m_projectPanel.Refresh();
    UpdateWindowTitle();
    return true;
}

void SceneDocument::OfferNewProjectInEmptyDir(const fs::path& emptyDir) {
    std::error_code ec;
    const fs::path abs = fs::absolute(emptyDir, ec).lexically_normal();
    const fs::path clean = abs.filename().empty() ? abs.parent_path() : abs;
    m_newProjectEmptyDir = ToUtf8(clean);
    m_newProjectLocation = ToUtf8(clean.parent_path());
    m_newProjectName = ToUtf8(clean.filename());
    if (!Project::ValidateName(m_newProjectName).empty()) m_newProjectName = "MeuJogo";
    m_openNewProjectPopup = true;
}

void SceneDocument::ApplyRecovery(const SceneSerializer::RecoveryInfo& recovery) {
    Scene recovered;
    std::string error;
    if (!SceneSerializer::Load(recovery.sceneFile, recovered, m_ctx.assets, &error)) {
        ShowError(error);
        return;
    }

    const fs::path targetPath = recovery.originalPath.empty() ? m_scenePath : recovery.originalPath;

    // Compara com a cena que está no disco para manter o '*' de alterações não salvas até o usuário salvar.
    std::string diskSnapshot;
    std::error_code ec;
    if (!targetPath.empty() && fs::is_regular_file(targetPath, ec)) {
        Scene onDisk;
        if (SceneSerializer::Load(targetPath, onDisk, m_ctx.assets)) {
            diskSnapshot = SceneSerializer::ToJson(onDisk, m_ctx.assets, {.pathHints = false});
        }
    }

    m_ctx.scene = std::move(recovered);
    m_ctx.selected = kNullEntity;
    m_ctx.pendingDelete = kNullEntity;
    m_ctx.eulerEntity = kNullEntity;
    m_scenePath = targetPath;
    m_history.Reset(m_ctx.scene, kNullEntity);
    m_savedSnapshot = std::move(diskSnapshot);
    m_dirty = SceneSerializer::ToJson(m_ctx.scene, m_ctx.assets, {.pathHints = false}) != m_savedSnapshot;
    m_autoSaveTimer = 0.0f;
    ClearRecovery();
    RememberScene();
    UpdateWindowTitle();
    log::Info("Cena recuperada de '{}'", ToUtf8(recovery.sceneFile));
}

void SceneDocument::CreateProject(const fs::path& location, const std::string& name) {
    std::string error;
    if (!CreateProjectAt(location, name, error)) ShowError(error);
}

bool SceneDocument::CreateProjectAt(const fs::path& location, const std::string& name, std::string& error) {
    auto project = Project::Create(location, name, &error);
    if (!project) return false;

    // Pasta de scripts com um exemplo, e a pasta gerada pelo editor fora do controle de versão.
    const ScriptProject::Paths scripts = ScriptProject::PathsFor(project->RootDir());
    std::string templateError;
    const bool withScript = ScriptProject::CopyTemplate(ScriptProject::DefaultTemplatesDir(), "Girar",
                                                        scripts.scriptsDir, &templateError);
    if (!withScript) log::Warn("Projeto criado sem o script de exemplo: {}", templateError);
    if (std::ofstream ignore(project->RootDir() / ".gitignore", std::ios::binary); ignore) {
        ignore << "# Gerado pelo Motor Editor (compilação dos scripts): pode ser apagado.\n.motor/\n";
    }

    Scene starter;
    BuildStarterScene(starter, m_ctx.assets, withScript);
    const fs::path scenePath = project->AssetsDir() / "Scenes" / "Principal.mscene";
    if (SceneSerializer::Save(scenePath, starter, m_ctx.assets, &error)) {
        project->SetStartupScene(scenePath);
        project->Save(&error);
    } else {
        log::Warn("Projeto criado sem cena inicial: {}", error);
    }

    if (!OpenProjectAt(project->ProjectFile(), false)) {
        error = std::format("Projeto criado em '{}', mas não foi possível abri-lo.", ToUtf8(project->RootDir()));
        return false;
    }
    return true;
}

void SceneDocument::OnPathMoved(const fs::path& from, const fs::path& to) {
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

void SceneDocument::OnPathDeleted(const fs::path& path) {
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
    if (m_host.exportGame && ImGui::MenuItem("Exportar jogo...", nullptr, false, m_project.has_value())) m_host.exportGame();
    ImGui::Separator();
    if (ImGui::MenuItem("Sair", "Alt+F4")) m_host.requestClose();  // passa por OnCloseRequested

    ImGui::EndMenu();
}

void SceneDocument::DrawPopups() {
    const char* unsavedTitle = "Alterações não salvas";
    const char* newerVersionTitle = "Projeto de versão mais nova";
    const char* recoveryTitle = "Recuperar alterações não salvas";
    const char* errorTitle = "Erro";
    if (m_openUnsavedPopup) {
        ImGui::OpenPopup(unsavedTitle);
        m_openUnsavedPopup = false;
    }
    if (m_openNewerVersionPopup) {
        ImGui::OpenPopup(newerVersionTitle);
        m_openNewerVersionPopup = false;
    }
    if (m_openRecoveryPopup) {
        ImGui::OpenPopup(recoveryTitle);
        m_openRecoveryPopup = false;
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
            ClearRecovery();
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

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(newerVersionTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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
        const bool cancel = ImGui::Button("Cancelar", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        const fs::path path = m_pendingNewerProjectPath;
        const bool restore = m_pendingNewerProjectRestoreScene;
        if (openAnyway || cancel) {
            ImGui::CloseCurrentPopup();
            m_pendingNewerProjectPath.clear();
        }
        ImGui::EndPopup();
        if (openAnyway) OpenProjectAt(path, restore, true);
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(recoveryTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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

        const bool recover = ImGui::Button("Recuperar", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Enter, false);
        ImGui::SameLine();
        const bool discard = ImGui::Button("Descartar", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
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
