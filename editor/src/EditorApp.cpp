#include "EditorApp.h"

#include <imgui.h>
#include <motor/core/Version.h>
#include <motor/scene/ModelImport.h>
#include <motor/scene/ScriptModule.h>
#include <imgui_internal.h>  // DockBuilder
#include <ImGuizmo.h>

#include <format>
#include <string>
#include <utility>

namespace editor {

using namespace motor;
namespace fs = std::filesystem;

namespace {
ApplicationDesc MakeDesc() {
    ApplicationDesc desc;
    desc.window.title = std::format("Motor Editor {}", kVersion);
    desc.window.width = 1600;
    desc.window.height = 900;
    desc.enableImGui = true;
    return desc;
}
} // namespace

EditorApp::EditorApp(Options options)
    : Application(MakeDesc()),
      m_document(m_ctx, m_history, m_settings, m_projectPanel,
                 {.isPlaying = [this] { return m_play.IsActive(); },
                  .stopPlay = [this] { StopPlay(); },
                  .quit = [this] { Quit(); },
                  .requestClose = [this] { GetWindow().RequestClose(); },
                  .setWindowTitle = [this](const std::string& title) { SetWindowTitle(title); },
                  .projectClosing = [this] { m_scripts.CloseProject(); },
                  .projectOpened = [this] { m_scripts.OpenProject(); },
                  .exportGame = [this] { m_export.Open(); }}),
      m_scripts(m_ctx, m_history, m_document, m_play),
      m_export({.ctx = m_ctx, .document = m_document, .scripts = m_scripts}),
      m_mcp({.ctx = m_ctx,
             .document = m_document,
             .play = m_play,
             .scripts = m_scripts,
             .viewport = m_viewport,
             .requestHistory = [this](int direction) { m_historyRequest = direction; },
             .exporter = m_export}),
      m_startupProject(std::move(options.project)),
      m_createParent(std::move(options.createParent)),
      m_createName(std::move(options.createName)),
      m_startupMcpPort(options.mcpPort) {}

void EditorApp::OnInit() {
    log::Info("Motor Editor {} (API de scripts {})", kVersion, kScriptApiVersion);
    if (EditorSettings::IsDisabled()) log::Info("Configuração do editor desligada (--no-settings)");
    m_viewport.Init();

    AddBuiltinAssets(m_ctx.assets);  // os mesmos do Player (jogo exportado)

    // Projeto da linha de comando, senão o último usado; sem projeto, mostra a cena de exemplo.
    m_settings = EditorSettings::Load();
    m_projectPanel.SetView({m_settings.projectGridView, m_settings.projectIconSize});
    std::error_code ec;
    bool opened = false;
    if (!m_createName.empty()) {
        // --create (Hub de projetos): o mesmo que Arquivo > Novo projeto; se falhar, avisa e segue sem projeto.
        std::string error;
        opened = m_document.CreateProjectAt(m_createParent, m_createName, error);
        if (!opened) m_document.ShowError(error);
    } else {
        const fs::path project = m_startupProject.empty() ? m_settings.lastProject : m_startupProject;
        opened = !project.empty() && fs::exists(project, ec) && m_document.OpenProjectAt(project, true);
    }
    if (!opened) {
        BuildDefaultScene();
        m_history.Reset(m_ctx.scene, kNullEntity);
        m_document.MarkSaved();  // a cena de exemplo começa "limpa"
    }

    log::Info("Editor: botão direito no viewport = voar (WASD/QE) | clique = selecionar | F = focar | "
              "Del = excluir | Ctrl+D = duplicar | Ctrl+P = Play");

    // Servidor MCP: pedido na linha de comando (--mcp) ou deixado ligado na última sessão.
    if (m_startupMcpPort > 0) {
        m_mcp.Start(m_startupMcpPort);
    } else if (m_settings.mcpEnabled) {
        m_mcp.Start(m_settings.mcpPort);
    }
}

void EditorApp::BuildDefaultScene() {
    Entity& floor = m_ctx.CreatePrimitive(Primitive::Plane);
    floor.name = "Chão";
    floor.transform.scale = {40.0f, 1.0f, 40.0f};
    floor.Get<MeshRenderer>()->material.albedo = m_ctx.assets.FindTexture("Xadrez");
    floor.Get<MeshRenderer>()->material.specular = 0.1f;

    Entity& cube = m_ctx.CreatePrimitive(Primitive::Cube);
    cube.transform.position = {-2.5f, 0.5f, 0.0f};
    cube.transform.SetEulerDegrees({0.0f, 30.0f, 0.0f});
    cube.Get<MeshRenderer>()->material.color = {0.95f, 0.35f, 0.30f, 1.0f};

    Entity& sphere = m_ctx.CreatePrimitive(Primitive::Sphere);
    sphere.name = "Jogador";
    sphere.transform.position = {0.0f, 0.6f, 0.0f};
    sphere.transform.scale = glm::vec3(1.2f);
    sphere.Get<MeshRenderer>()->material.color = {0.30f, 0.60f, 0.95f, 1.0f};
    sphere.Get<MeshRenderer>()->material.roughness = 0.3f;

    Entity& group = m_ctx.CreatePrimitive(Primitive::Empty);
    group.name = "Torre";
    group.transform.position = {2.8f, 0.0f, -0.5f};
    const EntityId groupId = group.id;

    struct Part { const char* name; Primitive type; glm::vec3 pos; float scale; glm::vec4 color; };
    const Part parts[] = {
        {"Base", Primitive::Cube, {0.0f, 0.5f, 0.0f}, 1.0f, {0.98f, 0.75f, 0.25f, 1.0f}},
        {"Meio", Primitive::Cube, {0.0f, 1.4f, 0.0f}, 0.8f, {0.35f, 0.80f, 0.45f, 1.0f}},
        {"Topo", Primitive::Sphere, {0.0f, 2.15f, 0.0f}, 0.7f, {0.70f, 0.45f, 0.90f, 1.0f}},
    };
    for (const Part& p : parts) {
        Entity& e = m_ctx.CreatePrimitive(p.type, groupId);
        e.name = p.name;
        e.transform.position = p.pos;
        e.transform.scale = glm::vec3(p.scale);
        e.Get<MeshRenderer>()->material.color = p.color;
    }

    // Sem projeto não há scripts (eles ficam na pasta Scripts/ de cada projeto).
    Entity& camera = m_ctx.CreatePrimitive(Primitive::Camera);
    camera.transform.position = {0.0f, 4.6f, 7.0f};
    camera.transform.SetEulerDegrees({-30.0f, 0.0f, 0.0f});

    Entity& sun = m_ctx.CreatePrimitive(Primitive::DirectionalLight);
    sun.transform.rotation = RotationLookingAt({-0.5f, -1.0f, -0.35f});
    m_ctx.selected = kNullEntity;
}

void EditorApp::OnFixedUpdate(float dt) {
    m_mcp.ApplyGameInput();  // teclas simuladas pelo servidor MCP, antes do jogo ler a entrada
    m_play.FixedUpdate(dt);
}

void EditorApp::OnUpdate(float dt) {
    m_mcp.ApplyGameInput();
    m_play.Update(dt, m_viewport.IsFocused());
    m_scripts.Update(dt);
    m_document.PollChanges(dt);

    // Arquivos novos ou movidos fora do editor: cria os .meta que faltam e atualiza os caminhos dos GUIDs.
    m_assetRefreshTimer += dt;
    if (m_assetRefreshTimer >= 2.0f) {
        m_assetRefreshTimer = 0.0f;
        m_ctx.assets.Database().Refresh();
        // Materiais e prefabs editados fora do editor (ou por outro programa): relidos no mesmo objeto.
        m_ctx.assets.ReloadChangedMaterials();
        // Modelos glTF novos ou alterados viram prefab (motor/scene/ModelImport.h).
        if (!m_play.IsActive()) motor::ImportModelsInProject(m_ctx.assets);
        m_ctx.prefabs.ReloadChanged(m_ctx.assets);
    }

    m_viewport.UpdateCamera(dt, GetWindow());
    if (GetWindow().IsMinimized()) UpdateMinimized();
}

void EditorApp::OnRender(Renderer& renderer) {
    m_viewport.Render(renderer, m_ctx, m_play.InGameView());
}

void EditorApp::ApplyRequests() {
    // No Play não há desfazer (tudo é descartado ao parar).
    if (!m_play.IsActive()) {
        if (m_historyRequest < 0) Undo();
        if (m_historyRequest > 0) Redo();
    }
    m_historyRequest = 0;
    ApplyPlayRequest();
}

void EditorApp::ApplyPendingChanges(bool interactionFinished) {
    m_ctx.ApplyPendingDelete();
    m_ctx.ApplyPendingPrefabAction();
    // Instâncias seguem os prefabs que mudaram; o que mudou numa instância vira override (antes do Track,
    // para entrar no mesmo passo de desfazer). No Play, não: a cena do jogo é descartada ao parar.
    if (!m_play.IsActive()) m_ctx.UpdatePrefabs([this](const auto& fn) { m_history.ForEachScene(fn); });
    // Depois de todas as alterações do frame: se a cena mudou e o usuário terminou a interação, vira um passo.
    // No Play, não: ao parar a cena volta a ser igual à última foto do histórico.
    if (!m_play.IsActive()) m_history.Track(m_ctx.scene, m_ctx.selected, interactionFinished);
}

void EditorApp::UpdateMinimized() {
    // Minimizado o OnImGui não roda: o que não é interface roda aqui, para o servidor MCP continuar atendendo
    // (antes os pedidos esperavam até a janela voltar, e o agente via o editor como travado).
    ApplyRequests();
    m_mcp.Pump();
    ApplyPendingChanges(true);  // ninguém está arrastando nada com a janela minimizada
}

void EditorApp::OnImGui() {
    // Desfazer/refazer e Play/Parar pedidos no frame anterior: aplicados antes de os painéis desenharem a cena.
    ApplyRequests();

    ImGuizmo::BeginFrame();
    DrawMenuBar();

    const ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");
    if (m_resetLayout) {
        // Só sobrescreve se não houver layout salvo (imgui.ini) ou se o usuário pediu para restaurar.
        // Também reconstrói quando o layout salvo é de uma versão sem os painéis "Projeto" ou "Scripts".
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
        const bool hasPanels = ImGui::FindWindowSettingsByID(ImHashStr("Projeto")) != nullptr &&
                               ImGui::FindWindowSettingsByID(ImHashStr("Scripts")) != nullptr;
        if (!node || !node->IsSplitNode() || !hasPanels) BuildDockLayout(dockspaceId);
        m_resetLayout = false;
    }
    ImGui::DockSpaceOverViewport(dockspaceId, ImGui::GetMainViewport());

    DrawHierarchyPanel(m_ctx);
    DrawScenePanel(m_ctx);
    DrawInspectorPanel(m_ctx);
    m_viewport.Draw(m_ctx, m_play, GetRenderer().GetStats(), FramesPerSecond());
    DrawProjectPanel();
    m_scripts.DrawPanel();
    if (m_showDemo) ImGui::ShowDemoWindow(&m_showDemo);

    // Atalhos antes dos popups: o Esc que fecha um popup não deve também limpar a seleção.
    HandleShortcuts();
    // Pedidos do servidor MCP: depois do viewport (que informa o mouse à UI do jogo) e antes do Track do fim do
    // quadro, que transforma as mudanças deles em passos de desfazer.
    m_mcp.Pump();
    if (!m_ctx.pendingError.empty()) m_document.ShowError(std::exchange(m_ctx.pendingError, {}));
    m_document.DrawPopups();
    m_scripts.DrawNewScriptPopup();
    m_export.DrawPopup();
    DrawAboutPopup();
    ApplyPendingChanges(!IsInteracting());
}

bool EditorApp::OnCloseRequested() {
    StopPlay();  // o que importa é a cena de antes do Play
    if (!m_document.HasUnsavedChanges()) return true;
    m_document.RequestFileAction(SceneDocument::FileAction::Quit);
    return false;
}

void EditorApp::OnShutdown() {
    m_mcp.Stop();  // antes de tudo: nenhuma ferramenta pode rodar com o editor fechando
    StopPlay();
    m_scripts.CloseProject();  // nenhum objeto da DLL de scripts pode sobrar depois de descarregá-la
    m_settings.projectGridView = m_projectPanel.View().grid;
    m_settings.projectIconSize = m_projectPanel.View().iconSize;
    m_settings.Save();
}

void EditorApp::ApplyPlayRequest() {
    switch (m_play.ApplyRequest(m_ctx, m_history)) {
        case PlayController::Event::None: break;
        case PlayController::Event::Started:
            m_viewport.StopLooking();
            m_viewport.RequestFocus();  // as teclas vão para o jogo sem precisar clicar no viewport
            break;
        case PlayController::Event::Stopped: OnPlayStopped(); break;
    }
}

void EditorApp::StopPlay() {
    if (m_play.Stop(m_ctx)) OnPlayStopped();
}

void EditorApp::OnPlayStopped() {
    m_viewport.StopLooking();
    m_mcp.OnPlayStopped();
    m_scripts.OnPlayStopped();  // scripts recompilados durante o Play recarregam agora
}

void EditorApp::BuildDockLayout(unsigned int dockspaceId) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, vp->WorkSize);

    ImGuiID center = dockspaceId;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
    ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f, nullptr, &left);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);

    ImGui::DockBuilderDockWindow("Hierarquia", left);
    ImGui::DockBuilderDockWindow("Cena", leftBottom);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Viewport", center);
    ImGui::DockBuilderDockWindow("Scripts", bottom);
    ImGui::DockBuilderDockWindow("Projeto", bottom);  // por último: fica como aba ativa
    ImGui::DockBuilderFinish(dockspaceId);
}

void EditorApp::DrawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;

    m_document.DrawFileMenu();
    if (ImGui::BeginMenu("Editar")) {
        const bool editing = !m_play.IsActive();
        if (ImGui::MenuItem("Desfazer", "Ctrl+Z", false, editing && m_history.CanUndo())) m_historyRequest = -1;
        if (ImGui::MenuItem("Refazer", "Ctrl+Y", false, editing && m_history.CanRedo())) m_historyRequest = +1;
        ImGui::Separator();
        const bool hasSelection = m_ctx.scene.Find(m_ctx.selected) != nullptr;
        if (ImGui::MenuItem("Duplicar", "Ctrl+D", false, hasSelection)) m_ctx.DuplicateSelected();
        if (ImGui::MenuItem("Excluir", "Del", false, hasSelection)) m_ctx.pendingDelete = m_ctx.selected;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Entidade")) {
        DrawCreateEntityMenuItems(m_ctx, kNullEntity);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ver")) {
        Renderer& r = GetRenderer();
        if (ImGui::MenuItem("Wireframe", nullptr, r.IsWireframe())) r.SetWireframe(!r.IsWireframe());
        if (ImGui::MenuItem("Focar seleção", "F", false, m_ctx.selected != kNullEntity)) m_viewport.FocusSelected(m_ctx);
        ImGui::Separator();
        if (ImGui::MenuItem("Restaurar layout")) {
            ImGui::DockBuilderRemoveNode(ImGui::GetID("EditorDockSpace"));
            m_resetLayout = true;
        }
        ImGui::MenuItem("Demo do ImGui", nullptr, &m_showDemo);
        ImGui::Separator();
        m_mcp.DrawMenuItems(m_settings);
        ImGui::EndMenu();
    }
    m_play.DrawMenu();
    m_scripts.DrawMenu();
    if (ImGui::BeginMenu("Ajuda")) {
        if (ImGui::MenuItem("Sobre o Motor")) m_openAbout = true;
        ImGui::EndMenu();
    }
    m_play.DrawStatus();
    m_scripts.DrawStatus();
    m_mcp.DrawStatus();
    ImGui::EndMainMenuBar();
}

void EditorApp::HandleShortcuts() {
    using FileAction = SceneDocument::FileAction;
    using PlayRequest = PlayController::Request;

    const ImGuiIO& io = ImGui::GetIO();
    if (m_viewport.IsLooking() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;

    // Atalhos de arquivo e de Play valem mesmo editando texto.
    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_N, false)) m_document.RequestFileAction(FileAction::NewScene);
        if (ImGui::IsKeyPressed(ImGuiKey_O, false)) m_document.RequestFileAction(FileAction::OpenScene);
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) m_document.SaveScene(io.KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_P, false)) {
            if (io.KeyShift) {
                m_play.Queue(PlayRequest::Pause);
            } else {
                m_play.Queue(m_play.IsActive() ? PlayRequest::Stop : PlayRequest::Play);
            }
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F8, false) && m_play.IsActive()) m_play.ToggleGameView();
    if (ImGui::IsKeyPressed(ImGuiKey_F7, false)) m_scripts.RequestBuild();
    // Jogando com o viewport em foco, as teclas são do jogo (WASD não troca o gizmo, Del não exclui...).
    if (m_play.InGameView() && m_viewport.IsFocused()) return;
    // Com o painel Projeto em foco, Del/F2/Enter pertencem a ele (arquivos), não às entidades.
    // Editando um campo de texto, Ctrl+Z/Ctrl+Y desfazem o texto (o próprio ImGui cuida).
    if (io.WantTextInput) return;
    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, true)) m_historyRequest = io.KeyShift ? +1 : -1;  // segurar repete
        if (ImGui::IsKeyPressed(ImGuiKey_Y, true)) m_historyRequest = +1;
    }
    if (m_projectPanel.IsFocused()) return;

    m_viewport.HandleGizmoShortcuts();

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && m_ctx.selected != kNullEntity) {
        m_ctx.pendingDelete = m_ctx.selected;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) m_ctx.DuplicateSelected();
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) m_viewport.FocusSelected(m_ctx);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_ctx.selected = kNullEntity;
}

bool EditorApp::IsInteracting() const {
    return ImGui::IsAnyItemActive() || ImGuizmo::IsUsing() || ImGui::IsMouseDown(ImGuiMouseButton_Left);
}

void EditorApp::Undo() {
    if (IsInteracting()) return;  // no meio de um arraste, desfazer bagunçaria o estado
    if (m_history.Undo(m_ctx.scene, m_ctx.selected)) {
        m_ctx.pendingDelete = kNullEntity;
        m_document.HasUnsavedChanges();  // atualiza o "*" do título na hora
    }
}

void EditorApp::Redo() {
    if (IsInteracting()) return;
    if (m_history.Redo(m_ctx.scene, m_ctx.selected)) {
        m_ctx.pendingDelete = kNullEntity;
        m_document.HasUnsavedChanges();
    }
}

void EditorApp::DrawAboutPopup() {
    constexpr const char* kTitle = "Sobre o Motor";
    if (m_openAbout) {
        ImGui::OpenPopup(kTitle);
        m_openAbout = false;
    }
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("Motor Editor %s", kVersion);
    ImGui::TextDisabled("API de scripts %d", kScriptApiVersion);
    ImGui::Separator();
    ImGui::TextUnformatted("Engine 3D em C++20 e OpenGL 4.5.");
    ImGui::TextUnformatted("Projetos e versões instaladas: Hub do Motor (MotorHub.exe).");
    ImGui::Text("Pasta do editor: %s", ToUtf8(ExecutableDir()).c_str());
    if (ImGui::Button("Fechar", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void EditorApp::DrawProjectPanel() {
    using FileAction = SceneDocument::FileAction;

    ProjectPanel::Callbacks callbacks;
    callbacks.createProject = [this] { m_document.RequestFileAction(FileAction::NewProject); };
    callbacks.openProject = [this] { m_document.RequestFileAction(FileAction::OpenProject); };
    callbacks.openScene = [this](const fs::path& p) { m_document.RequestFileAction(FileAction::OpenScenePath, p); };
    callbacks.createSceneFile = [this](const fs::path& p) { return m_document.CreateEmptySceneFile(p); };
    callbacks.pathMoved = [this](const fs::path& from, const fs::path& to) {
        m_document.OnPathMoved(from, to);
        m_ctx.inspectedAsset = RebasePath(m_ctx.inspectedAsset, from, to);
    };
    callbacks.pathDeleted = [this](const fs::path& p) { m_document.OnPathDeleted(p); };
    callbacks.addMeshToScene = [this](const fs::path& p) { m_ctx.CreateFromMeshFile(p); };
    callbacks.applyTexture = [this](const fs::path& p) {
        if (m_ctx.scene.Find(m_ctx.selected)) {
            m_ctx.ApplyTextureFile(m_ctx.selected, p);
        } else {
            m_document.ShowError("Selecione uma entidade para aplicar a textura.");
        }
    };

    callbacks.selectAsset = [this](const fs::path& p) {
        // Material e prefab aparecem no Inspector (no lugar da entidade selecionada).
        const AssetType type = AssetTypeFromPath(p);
        if (type != AssetType::Material && type != AssetType::Prefab) return;
        m_ctx.inspectedAsset = p;
        m_ctx.selected = kNullEntity;
    };
    callbacks.createMaterialFile = [this](const fs::path& p) {
        std::string error;
        if (m_ctx.assets.WriteMaterialFile(p, Material{}, &error)) return true;
        m_document.ShowError(error);
        return false;
    };
    callbacks.addPrefabToScene = [this](const fs::path& p) { m_ctx.InstantiatePrefabFile(p); };
    callbacks.addModelToScene = [this](const fs::path& p) { m_ctx.InstantiateModelFile(p); };
    callbacks.reimportModel = [this](const fs::path& p) { m_ctx.ReimportModelFile(p); };
    callbacks.createPrefabFromEntity = [this](uint32_t entity, const fs::path& folder) {
        m_ctx.CreatePrefabFromEntity(entity, folder);
    };

    // Arquivos arrastados do Explorer para a janela são importados pelo painel.
    if (auto dropped = GetWindow().TakeDroppedFiles(); !dropped.empty()) m_projectPanel.QueueImport(std::move(dropped));

    m_projectPanel.Draw(m_document.GetProject(), m_document.ScenePath(), callbacks);
}

} // namespace editor
