// Documento do editor: abrir e criar projetos, arquivos movidos e apagados no painel Projeto.

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

} // namespace editor
