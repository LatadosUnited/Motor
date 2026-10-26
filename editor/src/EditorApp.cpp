#include "EditorApp.h"

#include "FileDialog.h"

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
             .gameView = m_gameView,
             .requestHistory = [this](int direction) { m_historyRequest = direction; },
             .exporter = m_export,
             .renderer = [this]() -> motor::Renderer& { return GetRenderer(); }}),
      m_startupProject(std::move(options.project)),
      m_createParent(std::move(options.createParent)),
      m_createName(std::move(options.createName)),
      m_startupMcpPort(options.mcpPort),
      m_perfFile(std::move(options.perfFile)),
      m_quitAfterSeconds(options.seconds) {
    if (options.hubPort > 0) m_mcp.LinkHub(options.hubPort, std::move(options.hubToken));  // Hub residente
}

void EditorApp::OnInit() {
    m_ctx.pushAssetUndo = [this](std::function<void()> undo, std::function<void()> redo) {
        if (!m_play.IsActive()) {
            m_history.PushAction(m_ctx.scene, m_ctx.selected, &m_ctx.selection, std::move(undo), std::move(redo));
        }
    };
    m_history.SetLazyCompare(true);  // compara a cena só depois de entrada ou pedido do MCP (Fase 27)
    log::Info("Motor Editor {} (API de scripts {})", kVersion, kScriptApiVersion);
    if (EditorSettings::IsDisabled()) log::Info("Configuração do editor desligada (--no-settings)");
    m_viewport.Init();
    m_gameView.Init();

    AddBuiltinAssets(m_ctx.assets);  // os mesmos do Player (jogo exportado)

    // Projeto da linha de comando, senão o último usado; sem projeto, mostra a cena de exemplo.
    m_settings = EditorSettings::Load();
    m_projectPanel.SetView({m_settings.projectGridView, m_settings.projectIconSize});
    m_viewport.SetPrefs(m_settings.viewport);
    m_play.SetSideBySide(m_settings.playSideBySide);
    std::error_code ec;
    bool opened = false;
    if (!m_createName.empty()) {
        // --create (Hub de projetos): o mesmo que Arquivo > Novo projeto; se falhar, avisa e segue sem projeto.
        std::string error;
        opened = m_document.CreateProjectAt(m_createParent, m_createName, error);
        if (!opened) m_document.ShowError(error);
    } else {
        fs::path project = m_startupProject.empty() ? m_settings.lastProject : m_startupProject;
        if (project.empty()) {
            const fs::path cwd = fs::current_path(ec);
            if (!ec && fs::is_directory(cwd, ec) && fs::is_empty(cwd, ec)) project = cwd;
        }
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
    // Em segundo plano (ex.: você no VS Code) e durante a compilação dos scripts, fora do Play, o editor desenha
    // menos: o processador fica para o compilador, que levava o triplo do tempo com o editor a 60 FPS (a placa
    // de vídeo integrada divide a energia com o processador). O jogo no Play continua na velocidade normal.
    // Medindo (--perf), sem o limite de segundo plano: a janela aberta por um script nem sempre fica com o foco.
    if (m_play.IsActive()) {
        SetFrameLimit(0.0);
    } else if (!GetWindow().IsFocused() && m_perfFile.empty()) {
        SetFrameLimit(10.0);
    } else {
        SetFrameLimit(m_scripts.IsBuilding() ? 20.0 : 0.0);  // um segundo de "Compilando..." não precisa de 60 FPS
    }
    // Parado (ninguém mexe, fora do Play), o editor dorme até o próximo evento em vez de redesenhar a cena e os
    // painéis na taxa do monitor; acorda pelo menos 4 vezes por segundo para o que muda sozinho (arquivos
    // alterados fora, o cursor piscando). O servidor MCP acorda na hora (Window::Wake).
    const bool busy = m_play.IsActive() || m_scripts.IsBuilding() || m_mcp.HasPending() || m_export.IsBusy() ||
                      !m_perfFile.empty();
    SetIdleWait(busy ? 0.0 : 0.25);
    m_mcp.ApplyGameInput();
    if (motor::SceneRuntime* runtime = m_play.Runtime()) runtime->SetFixedAlpha(FixedAlpha());  // interpolação
    m_play.Update(dt, m_viewport.IsFocused(), m_gameView.IsFocused(), GetWindow());
    m_scripts.Update(dt);
    {
        MOTOR_PROFILE_STAT("Update: checar não salvo");
        m_document.PollChanges(dt);
    }

    // Arquivos novos ou movidos fora do editor: cria os .meta que faltam e atualiza os caminhos dos GUIDs.
    // Só quando algo mudou na pasta do projeto (FolderWatcher): varrer os arquivos custava ~150 ms a cada 2 s.
    m_assetRefreshTimer += dt;
    if (m_assetRefreshTimer >= 2.0f) {
        m_assetRefreshTimer = 0.0f;
        if (m_assetWatcher.Folder() != m_ctx.assets.Root()) m_assetWatcher.Watch(m_ctx.assets.Root());
        if (m_assetWatcher.Changed()) m_assetsChanged = true;
    }
    if (m_assetsChanged && m_assetRefreshTimer == 0.0f) {  // no quadro em que o intervalo fechou
        m_assetsChanged = m_play.IsActive();  // no Play os modelos não são importados: confere de novo depois
        {
            MOTOR_PROFILE_STAT("Update: assets (.meta)");
            m_ctx.assets.Database().Refresh();
        }
        // Materiais e prefabs editados fora do editor (ou por outro programa): relidos no mesmo objeto.
        {
            MOTOR_PROFILE_STAT("Update: assets (materiais)");
            m_ctx.assets.ReloadChangedMaterials();
        }
        // Modelos glTF novos ou alterados viram prefab (motor/scene/ModelImport.h).
        {
            MOTOR_PROFILE_STAT("Update: assets (modelos)");
            if (!m_play.IsActive()) motor::ImportModelsInProject(m_ctx.assets);
        }
        {
            MOTOR_PROFILE_STAT("Update: assets (prefabs)");
            m_ctx.prefabs.ReloadChanged(m_ctx.assets);
        }
        m_history.NoteActivity();  // instâncias de um prefab alterado mudam no UpdatePrefabs
    }

    m_viewport.UpdateCamera(dt, GetWindow());
    if (GetWindow().IsMinimized()) UpdateMinimized();

    if (!m_perfFile.empty()) {
        const FrameStats::Frame& last = FrameStats::Instance().Last();  // o quadro anterior, já fechado
        if (last.index != 0) m_perf.Record(last);
    }
    if (m_quitAfterSeconds > 0.0 && ElapsedTime() >= m_quitAfterSeconds) {
        log::Info("Editor: {:.1f} s rodados (--segundos), fechando", ElapsedTime());
        Quit();
    }
}

void EditorApp::OnRender(Renderer& renderer) {
    m_play.ApplyQuality(renderer);
    m_viewport.Render(renderer, m_ctx, m_play.InGameView());
    if (m_play.SideBySide()) m_gameView.Render(renderer, m_ctx);
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
    m_ctx.SyncSelection();
    m_ctx.ApplyPendingPrefabAction();
    // Instâncias seguem os prefabs que mudaram; o que mudou numa instância vira override (antes do Track,
    // para entrar no mesmo passo de desfazer). No Play, não: a cena do jogo é descartada ao parar.
    if (!m_play.IsActive()) m_ctx.UpdatePrefabs([this](const auto& fn) { m_history.ForEachScene(fn); });
    // Depois de todas as alterações do frame: se a cena mudou e o usuário terminou a interação, vira um passo.
    // No Play, não: ao parar a cena volta a ser igual à última foto do histórico.
    if (!m_play.IsActive()) m_history.Track(m_ctx.scene, m_ctx.selected, interactionFinished, &m_ctx.selection);
}

void EditorApp::UpdateMinimized() {
    // Minimizado o OnImGui não roda: o que não é interface roda aqui, para o servidor MCP continuar atendendo
    // (antes os pedidos esperavam até a janela voltar, e o agente via o editor como travado).
    ApplyRequests();
    if (m_mcp.Pump()) {
        m_history.NoteActivity();
        NoteActivity();  // a resposta pode precisar dos próximos quadros (captura de tela, Play)
    }
    ApplyPendingChanges(true);  // ninguém está arrastando nada com a janela minimizada
}

void EditorApp::OnImGui() {
    // Desfazer/refazer e Play/Parar pedidos no frame anterior: aplicados antes de os painéis desenharem a cena.
    ApplyRequests();
    // O desfazer só compara a cena depois de alguma entrada (tecla, clique, roda, texto): mexer o mouse não muda
    // nada, e com milhares de entidades a comparação custa milissegundos (Fase 27).
    for (const ImGuiInputEvent& event : ImGui::GetCurrentContext()->InputEventsTrail) {
        if (event.Type != ImGuiInputEventType_MousePos && event.Type != ImGuiInputEventType_MouseViewport) {
            m_history.NoteActivity();
            break;
        }
    }
    m_ctx.playing = m_play.IsActive();
    m_ctx.SyncSelection();  // seleção trocada por fora (criar, MCP, desfazer) antes dos painéis

    ImGuizmo::BeginFrame();
    {
        MOTOR_PROFILE_STAT("ImGui: menus");
        DrawMenuBar();
    }

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
    m_gameView.PrepareDock(m_play);

    // Cada painel com o seu tempo de CPU no --perf e no Tracy (Fase 27: o editor com milhares de entidades).
    {
        MOTOR_PROFILE_STAT("ImGui: Hierarquia");
        DrawHierarchyPanel(m_ctx);
    }
    {
        MOTOR_PROFILE_STAT("ImGui: Cena");
        DrawScenePanel(m_ctx);
    }
    {
        MOTOR_PROFILE_STAT("ImGui: Inspector");
        DrawInspectorPanel(m_ctx);
    }
    {
        MOTOR_PROFILE_STAT("ImGui: Viewport");
        m_viewport.Draw(m_ctx, m_play, GetRenderer().GetStats(), FramesPerSecond());
        m_gameView.Draw(m_play);
    }
    {
        MOTOR_PROFILE_STAT("ImGui: Projeto");
        DrawProjectPanel();
    }
    {
        MOTOR_PROFILE_STAT("ImGui: Scripts");
        m_scripts.DrawPanel();
    }
    if (m_showDemo) ImGui::ShowDemoWindow(&m_showDemo);

    // Atalhos antes dos popups: o Esc que fecha um popup não deve também limpar a seleção.
    {
        MOTOR_PROFILE_STAT("ImGui: atalhos");
        HandleShortcuts();
    }
    // Pedidos do servidor MCP: depois do viewport (que informa o mouse à UI do jogo) e antes do Track do fim do
    // quadro, que transforma as mudanças deles em passos de desfazer.
    {
        MOTOR_PROFILE_STAT("ImGui: MCP");
        if (m_mcp.Pump()) {  // as ferramentas mexem na cena
            m_history.NoteActivity();
            NoteActivity();  // a resposta pode precisar dos próximos quadros (captura de tela, Play)
        }
    }
    MOTOR_PROFILE_STAT("ImGui: popups");
    if (!m_ctx.pendingError.empty()) m_document.ShowError(std::exchange(m_ctx.pendingError, {}));
    m_document.DrawPopups();
    m_scripts.DrawNewScriptPopup();
    m_export.DrawPopup();
    DrawAboutPopup();
    {
        MOTOR_PROFILE_STAT("ImGui: desfazer e prefabs");
        ApplyPendingChanges(!IsInteracting());
    }
}

bool EditorApp::OnCloseRequested() {
    StopPlay();  // o que importa é a cena de antes do Play
    if (!m_document.HasUnsavedChanges()) {
        m_document.ClearRecovery();
        return true;
    }
    m_document.RequestFileAction(SceneDocument::FileAction::Quit);
    return false;
}

void EditorApp::OnShutdown() {
    m_mcp.Stop();  // antes de tudo: nenhuma ferramenta pode rodar com o editor fechando
    StopPlay();
    m_scripts.CloseProject();  // nenhum objeto da DLL de scripts pode sobrar depois de descarregá-la
    m_settings.projectGridView = m_projectPanel.View().grid;
    m_settings.projectIconSize = m_projectPanel.View().iconSize;
    m_settings.viewport = m_viewport.Prefs();
    m_settings.playSideBySide = m_play.SideBySide();
    m_settings.Save();
    if (!m_perfFile.empty()) {
        std::string error;
        if (m_perf.Save(m_perfFile, &error)) {
            log::Info("Contadores: {} quadros em {}", m_perf.FrameCount(), ToUtf8(m_perfFile));
        } else {
            log::Error("--perf: não foi possível gravar os contadores: {}", error);
        }
    }
}

void EditorApp::ApplyPlayRequest() {
    const motor::Project* project = m_document.GetProject();
    if (project) {
        m_play.SetGameInfo({project->DefaultOptions(), project->Identity().version,
                            project->RootDir() / ".motor" / "saves", project->Actions()});
    } else {
        m_play.SetGameInfo({});
    }
    switch (m_play.ApplyRequest(m_ctx, m_history)) {
        case PlayController::Event::None: break;
        case PlayController::Event::Started:
            m_document.SaveRecovery("play", &m_play.BackupScene());
            m_viewport.StopLooking();
            // As teclas vão para o jogo sem precisar clicar: na janela Jogo, se estiver lado a lado.
            if (m_play.SideBySide()) {
                m_gameView.RequestFocus();
            } else {
                m_viewport.RequestFocus();
            }
            break;
        case PlayController::Event::Stopped: OnPlayStopped(); break;
    }
}

void EditorApp::StopPlay() {
    if (m_play.Stop(m_ctx)) OnPlayStopped();
}

void EditorApp::OnPlayStopped() {
    m_ctx.playing = false;
    RevertPlayAssetEdits(m_ctx);
    m_viewport.StopLooking();
    m_mcp.OnPlayStopped();
    m_scripts.OnPlayStopped();  // scripts recompilados durante o Play recarregam agora
    if (!m_document.HasUnsavedChanges()) m_document.ClearRecovery();
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
        if (ImGui::MenuItem("Recortar", "Ctrl+X", false, hasSelection)) m_ctx.CutSelected();
        if (ImGui::MenuItem("Copiar", "Ctrl+C", false, hasSelection)) m_ctx.CopySelected();
        if (ImGui::MenuItem("Colar", "Ctrl+V", false, !m_ctx.clipboard.Empty())) m_ctx.Paste();
        if (ImGui::MenuItem("Duplicar", "Ctrl+D", false, hasSelection)) m_ctx.DuplicateSelected();
        if (ImGui::MenuItem("Excluir", "Del", false, hasSelection)) m_ctx.pendingDelete = m_ctx.selected;
        ImGui::Separator();
        if (ImGui::MenuItem("Selecionar tudo", "Ctrl+A", false, editing)) SelectAll();
        if (ImGui::MenuItem("Buscar na hierarquia", "Ctrl+F")) FocusHierarchySearch();
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
        const auto openDoc = [this](const char* file, const char* label) {
            std::error_code ec;
            const fs::path path = EngineDocsDir() / file;
            if (fs::is_regular_file(path, ec)) {
                Shell::OpenWithDefaultApp(path);
            } else {
                m_document.ShowError(std::format("{} não encontrado em '{}'.", label, ToUtf8(path)));
            }
        };
        if (ImGui::MenuItem("Guia de scripts")) openDoc("scripts.md", "Guia de scripts");
        if (ImGui::MenuItem("Referência da API de scripts")) openDoc("scripts-api.md", "Referência da API de scripts");
        ImGui::Separator();
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
    if (m_play.GameHasInput()) return;
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
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) SelectAll();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) m_ctx.CopySelected();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false)) m_ctx.CutSelected();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) m_ctx.Paste();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) FocusHierarchySearch();
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) m_viewport.FocusSelected(m_ctx);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_ctx.SelectOnly(kNullEntity);
}

void EditorApp::SelectAll() {
    // Todas as entidades da cena, menos os elementos de UI (ficam na tela, não no mundo).
    std::vector<EntityId> ids;
    for (const auto& e : m_ctx.scene.Entities()) {
        if (!IsUiEntity(*e)) ids.push_back(e->id);
    }
    m_ctx.SetSelection(ids);
}

bool EditorApp::IsInteracting() const {
    return ImGui::IsAnyItemActive() || ImGuizmo::IsUsing() || ImGui::IsMouseDown(ImGuiMouseButton_Left);
}

void EditorApp::Undo() {
    if (IsInteracting()) return;  // no meio de um arraste, desfazer bagunçaria o estado
    if (m_history.Undo(m_ctx.scene, m_ctx.selected, &m_ctx.selection)) {
        m_ctx.pendingDelete = kNullEntity;
        m_document.HasUnsavedChanges();  // atualiza o "*" do título na hora
    }
}

void EditorApp::Redo() {
    if (IsInteracting()) return;
    if (m_history.Redo(m_ctx.scene, m_ctx.selected, &m_ctx.selection)) {
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
    callbacks.createScript = [this] { m_scripts.OpenNewScriptPopup(); };
    callbacks.openScript = [this](const fs::path& p) { m_scripts.OpenScript(p); };

    // Arquivos arrastados do Explorer para a janela são importados pelo painel.
    if (auto dropped = GetWindow().TakeDroppedFiles(); !dropped.empty()) m_projectPanel.QueueImport(std::move(dropped));

    m_projectPanel.Draw(m_document.GetProject(), m_document.ScenePath(), callbacks);
}

} // namespace editor
