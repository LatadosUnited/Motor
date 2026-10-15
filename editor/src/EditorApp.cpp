#include "EditorApp.h"

#include "FileDialog.h"

#include <imgui.h>
#include <imgui_internal.h>  // DockBuilder
#include <ImGuizmo.h>

#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <format>
#include <utility>

namespace editor {

using namespace motor;

namespace {
ApplicationDesc MakeDesc() {
    ApplicationDesc desc;
    desc.window.title = "Motor Editor";
    desc.window.width = 1600;
    desc.window.height = 900;
    desc.enableImGui = true;
    return desc;
}

constexpr glm::vec4 kSelectionColor{1.0f, 0.62f, 0.15f, 1.0f};
} // namespace

EditorApp::EditorApp(std::filesystem::path projectToOpen)
    : Application(MakeDesc()), m_startupProject(std::move(projectToOpen)) {}

void EditorApp::OnInit() {
    m_viewport = std::make_unique<Framebuffer>(1280, 720, 4);

    AssetLibrary& assets = m_ctx.assets;
    assets.AddMesh("Cubo", Mesh::CreateCube());
    assets.AddMesh("Esfera", Mesh::CreateSphere());
    assets.AddMesh("Plano", Mesh::CreatePlane());
    assets.AddTexture("Xadrez", Texture2D::CreateChecker(512, 16, {215, 215, 220, 255}, {150, 155, 165, 255}));
    assets.AddTexture("Xadrez grosso", Texture2D::CreateChecker(256, 4, {240, 240, 240, 255}, {60, 60, 70, 255}));

    m_camera.position = {0.0f, 4.0f, 9.0f};
    m_camera.LookAt({0.0f, 0.5f, 0.0f});

    // Projeto da linha de comando, senão o último usado; sem projeto, mostra a cena de exemplo.
    m_settings = EditorSettings::Load();
    m_projectPanel.SetView({m_settings.projectGridView, m_settings.projectIconSize});
    std::error_code ec;
    const std::filesystem::path project = m_startupProject.empty() ? m_settings.lastProject : m_startupProject;
    const bool opened = !project.empty() && std::filesystem::exists(project, ec) && OpenProjectAt(project, true);
    if (!opened) {
        BuildDefaultScene();
        m_history.Reset(m_ctx.scene, kNullEntity);
        MarkSaved();  // a cena de exemplo começa "limpa"
    }

    log::Info("Editor: botão direito no viewport = voar (WASD/QE) | clique = selecionar | F = focar | "
              "Del = excluir | Ctrl+D = duplicar");
}

void EditorApp::BuildDefaultScene() {
    Scene& scene = m_ctx.scene;

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
    sphere.transform.position = {0.0f, 0.6f, 0.0f};
    sphere.transform.scale = glm::vec3(1.2f);
    sphere.Get<MeshRenderer>()->material.color = {0.30f, 0.60f, 0.95f, 1.0f};
    sphere.Get<MeshRenderer>()->material.shininess = 64.0f;

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

    scene.light.direction = {-0.5f, -1.0f, -0.35f};
    m_ctx.selected = kNullEntity;
}

void EditorApp::OnUpdate(float dt) {
    // Detecção de alterações: compara o JSON da cena com o salvo a cada meio segundo (cenas pequenas, custo baixo).
    m_dirtyCheckTimer += dt;
    if (m_dirtyCheckTimer >= 0.5f) {
        m_dirtyCheckTimer = 0.0f;
        HasUnsavedChanges();
    }

    // Arquivos novos ou movidos fora do editor: cria os .meta que faltam e atualiza os caminhos dos GUIDs.
    m_assetRefreshTimer += dt;
    if (m_assetRefreshTimer >= 2.0f) {
        m_assetRefreshTimer = 0.0f;
        m_ctx.assets.Database().Refresh();
    }

    if (m_looking && !Input::IsMouseDown(MouseButton::Right)) m_looking = false;

    if (m_looking) {
        m_cameraController.Update(dt, GetWindow());
    } else {
        GetWindow().SetCursorCaptured(false);
    }

    // Enquanto voa com a câmera, o ImGui não deve reagir ao mouse.
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiConfigFlags mouseFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
    if (m_looking) {
        io.ConfigFlags |= mouseFlags;
    } else {
        io.ConfigFlags &= ~mouseFlags;
    }
}

void EditorApp::OnRender(Renderer& renderer) {
    m_viewport->Resize(static_cast<int>(m_viewportSize.x), static_cast<int>(m_viewportSize.y));
    m_camera.aspect = m_viewport->AspectRatio();

    const Scene& scene = m_ctx.scene;
    renderer.BeginScene(m_camera, scene.light, scene.environment, m_viewport.get());
    scene.SubmitTo(renderer);

    // Contorno em wireframe da seleção (e de seus descendentes).
    for (const auto& e : scene.Entities()) {
        const MeshRenderer* mr = e->Get<MeshRenderer>();
        if (!mr || !mr->mesh) continue;
        if (e->id != m_ctx.selected && !scene.IsDescendantOf(e->id, m_ctx.selected)) continue;
        renderer.SubmitWireframe(*mr->mesh, scene.WorldMatrix(e->id), kSelectionColor);
    }
    renderer.EndScene();
}

void EditorApp::OnImGui() {
    // Desfazer/refazer pedido no frame anterior: aplicado antes de os painéis desenharem a cena.
    if (m_historyRequest < 0) Undo();
    if (m_historyRequest > 0) Redo();
    m_historyRequest = 0;

    ImGuizmo::BeginFrame();
    DrawMenuBar();

    const ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");
    if (m_resetLayout) {
        // Só sobrescreve se não houver layout salvo (imgui.ini) ou se o usuário pediu para restaurar.
        // Também reconstrói quando o layout salvo é de uma versão sem o painel "Projeto".
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
        const bool hasProjectPanel = ImGui::FindWindowSettingsByID(ImHashStr("Projeto")) != nullptr;
        if (!node || !node->IsSplitNode() || !hasProjectPanel) BuildDockLayout(dockspaceId);
        m_resetLayout = false;
    }
    ImGui::DockSpaceOverViewport(dockspaceId, ImGui::GetMainViewport());

    DrawHierarchyPanel(m_ctx);
    DrawScenePanel(m_ctx);
    DrawInspectorPanel(m_ctx);
    DrawViewport();
    DrawProjectPanel();
    if (m_showDemo) ImGui::ShowDemoWindow(&m_showDemo);

    // Atalhos antes dos popups: o Esc que fecha um popup não deve também limpar a seleção.
    HandleShortcuts();
    if (!m_ctx.pendingError.empty()) ShowError(std::exchange(m_ctx.pendingError, {}));
    DrawFilePopups();
    m_ctx.ApplyPendingDelete();
    // Depois de todas as alterações do frame: se a cena mudou e o usuário terminou a interação, vira um passo.
    m_history.Track(m_ctx.scene, m_ctx.selected, !IsInteracting());
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
    ImGui::DockBuilderDockWindow("Projeto", bottom);
    ImGui::DockBuilderFinish(dockspaceId);
}

void EditorApp::DrawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;

    DrawFileMenu();
    if (ImGui::BeginMenu("Editar")) {
        if (ImGui::MenuItem("Desfazer", "Ctrl+Z", false, m_history.CanUndo())) m_historyRequest = -1;
        if (ImGui::MenuItem("Refazer", "Ctrl+Y", false, m_history.CanRedo())) m_historyRequest = +1;
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
        if (ImGui::MenuItem("Focar seleção", "F", false, m_ctx.selected != kNullEntity)) FocusSelected();
        ImGui::Separator();
        if (ImGui::MenuItem("Restaurar layout")) {
            ImGui::DockBuilderRemoveNode(ImGui::GetID("EditorDockSpace"));
            m_resetLayout = true;
        }
        ImGui::MenuItem("Demo do ImGui", nullptr, &m_showDemo);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void EditorApp::DrawViewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin("Viewport");
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    m_viewportSize = {std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)};

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const auto texture = static_cast<ImTextureID>(static_cast<uintptr_t>(m_viewport->ColorTexture()));
    // O OpenGL guarda as linhas de baixo para cima: invertemos o V.
    ImGui::Image(ImTextureRef(texture), ImVec2(m_viewportSize.x, m_viewportSize.y), ImVec2(0, 1), ImVec2(1, 0));
    const bool imageHovered = ImGui::IsItemHovered();
    HandleViewportDrop(origin);

    DrawGizmo(origin);
    DrawViewportToolbar(ImVec2(origin.x + 8.0f, origin.y + 44.0f));

    // Cliques que caem no gizmo ou na barra de ferramentas não chegam à cena.
    const bool sceneHovered = imageHovered && !ImGui::IsAnyItemHovered() &&
                              !ImGuizmo::IsOver() && !ImGuizmo::IsUsing();
    if (sceneHovered) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_looking = true;
            ImGui::SetWindowFocus();
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetMousePos();
            PickAt({(mouse.x - origin.x) / m_viewportSize.x, (mouse.y - origin.y) / m_viewportSize.y});
        }
    }

    // Overlay com estatísticas.
    const RenderStats& stats = GetRenderer().GetStats();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char text[160];
    std::snprintf(text, sizeof(text), "%.0f FPS  |  %u draws  |  %u tris  |  %dx%d", FramesPerSecond(),
                  stats.drawCalls, stats.triangles, m_viewport->Width(), m_viewport->Height());
    const ImVec2 pad{8.0f, 6.0f};
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 p0{origin.x + 8.0f, origin.y + 8.0f};
    dl->AddRectFilled(p0, ImVec2(p0.x + size.x + pad.x * 2, p0.y + size.y + pad.y * 2), IM_COL32(0, 0, 0, 120), 4.0f);
    dl->AddText(ImVec2(p0.x + pad.x, p0.y + pad.y), IM_COL32(235, 235, 235, 255), text);

    ImGui::End();
}

void EditorApp::DrawGizmo(const ImVec2& origin) {
    const EntityId selected = m_ctx.selected;
    if (m_gizmoTool == GizmoTool::None || !m_ctx.scene.Find(selected)) return;

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetGizmoSizeClipSpace(0.15f);  // padrão 0.1 fica pequeno no viewport
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(origin.x, origin.y, m_viewportSize.x, m_viewportSize.y);
    ImGuizmo::Enable(!m_looking);

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    float snapStep = m_snapTranslate;
    if (m_gizmoTool == GizmoTool::Rotate) {
        op = ImGuizmo::ROTATE;
        snapStep = m_snapRotate;
    } else if (m_gizmoTool == GizmoTool::Scale) {
        op = ImGuizmo::SCALE;
        snapStep = m_snapScale;
    }
    const ImGuizmo::MODE mode = m_gizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

    // Snap fixo pelo botão da barra, ou temporário segurando Ctrl.
    const bool snap = m_snap || ImGui::GetIO().KeyCtrl;
    const float snapValues[3] = {snapStep, snapStep, snapStep};

    const glm::mat4 view = m_camera.View();
    const glm::mat4 projection = m_camera.Projection();
    glm::mat4 world = m_ctx.scene.WorldMatrix(selected);

    if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), op, mode, glm::value_ptr(world),
                             nullptr, snap ? snapValues : nullptr)) {
        m_ctx.scene.SetWorldMatrix(selected, world);
    }
}

void EditorApp::DrawViewportToolbar(const ImVec2& position) {
    struct ToolButton { const char* label; GizmoTool tool; const char* tip; };
    const ToolButton tools[] = {
        {"Selecionar", GizmoTool::None, "Sem gizmo (Q)"},
        {"Mover", GizmoTool::Translate, "Mover (W)"},
        {"Rotacionar", GizmoTool::Rotate, "Rotacionar (E)"},
        {"Escalar", GizmoTool::Scale, "Escalar (R)"},
    };

    const ImVec4 activeColor = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    auto toggleButton = [&](const char* label, bool active) {
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
        const bool clicked = ImGui::Button(label);
        if (active) ImGui::PopStyleColor();
        return clicked;
    };

    // Fundo translúcido desenhado atrás dos botões (canal 0) depois de sabermos o tamanho do grupo.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);

    ImGui::SetCursorScreenPos(ImVec2(position.x + 6.0f, position.y + 5.0f));
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
    for (const ToolButton& t : tools) {
        if (toggleButton(t.label, m_gizmoTool == t.tool)) m_gizmoTool = t.tool;
        ImGui::SetItemTooltip("%s", t.tip);
        ImGui::SameLine();
    }
    ImGui::SameLine(0.0f, 14.0f);
    if (toggleButton(m_gizmoLocal ? "Local" : "Global", false)) m_gizmoLocal = !m_gizmoLocal;
    ImGui::SetItemTooltip("Eixos do gizmo: locais do objeto ou do mundo (X)");
    ImGui::SameLine();
    if (toggleButton("Snap", m_snap)) m_snap = !m_snap;
    ImGui::SetItemTooltip("Encaixe: %.2g m / %.0f° / %.2g (segure Ctrl para ativar temporariamente)",
                          m_snapTranslate, m_snapRotate, m_snapScale);
    ImGui::PopStyleVar();
    ImGui::EndGroup();

    const ImVec2 groupMax = ImGui::GetItemRectMax();
    dl->ChannelsSetCurrent(0);
    dl->AddRectFilled(position, ImVec2(groupMax.x + 6.0f, groupMax.y + 5.0f), IM_COL32(0, 0, 0, 120), 4.0f);
    dl->ChannelsMerge();
}

void EditorApp::HandleShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    if (m_looking || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;

    // Atalhos de arquivo valem mesmo editando texto.
    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_N, false)) RequestFileAction(FileAction::NewScene);
        if (ImGui::IsKeyPressed(ImGuiKey_O, false)) RequestFileAction(FileAction::OpenScene);
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) SaveScene(io.KeyShift);
    }
    // Com o painel Projeto em foco, Del/F2/Enter pertencem a ele (arquivos), não às entidades.
    // Editando um campo de texto, Ctrl+Z/Ctrl+Y desfazem o texto (o próprio ImGui cuida).
    if (io.WantTextInput) return;
    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, true)) m_historyRequest = io.KeyShift ? +1 : -1;  // segurar repete
        if (ImGui::IsKeyPressed(ImGuiKey_Y, true)) m_historyRequest = +1;
    }
    if (m_projectPanel.IsFocused()) return;

    // Ferramentas de transform (padrão Unity). Não conflita com WASD, que só vale com o botão direito segurado.
    if (!io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) m_gizmoTool = GizmoTool::None;
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_gizmoTool = GizmoTool::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_gizmoTool = GizmoTool::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_gizmoTool = GizmoTool::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) m_gizmoLocal = !m_gizmoLocal;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && m_ctx.selected != kNullEntity) {
        m_ctx.pendingDelete = m_ctx.selected;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) m_ctx.DuplicateSelected();
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) FocusSelected();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_ctx.selected = kNullEntity;
}

std::pair<glm::vec3, glm::vec3> EditorApp::ViewportRay(const glm::vec2& uv) const {
    const glm::vec2 ndc{uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f};
    const glm::mat4 invViewProj = glm::inverse(m_camera.Projection() * m_camera.View());

    glm::vec4 nearPoint = invViewProj * glm::vec4(ndc, -1.0f, 1.0f);
    glm::vec4 farPoint = invViewProj * glm::vec4(ndc, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    return {glm::vec3(nearPoint), glm::vec3(farPoint - nearPoint)};
}

bool EditorApp::IsInteracting() const {
    return ImGui::IsAnyItemActive() || ImGuizmo::IsUsing() || ImGui::IsMouseDown(ImGuiMouseButton_Left);
}

void EditorApp::Undo() {
    if (IsInteracting()) return;  // no meio de um arraste, desfazer bagunçaria o estado
    if (m_history.Undo(m_ctx.scene, m_ctx.selected)) {
        m_ctx.pendingDelete = kNullEntity;
        HasUnsavedChanges();  // atualiza o "*" do título na hora
    }
}

void EditorApp::Redo() {
    if (IsInteracting()) return;
    if (m_history.Redo(m_ctx.scene, m_ctx.selected)) {
        m_ctx.pendingDelete = kNullEntity;
        HasUnsavedChanges();
    }
}

void EditorApp::PickAt(const glm::vec2& uv) {
    const auto [origin, direction] = ViewportRay(uv);
    const auto hit = m_ctx.scene.Raycast(origin, direction);
    m_ctx.selected = hit ? hit->entity : kNullEntity;
}

void EditorApp::HandleViewportDrop(const ImVec2& origin) {
    // Arquivos do painel Projeto: malha = nova entidade onde o mouse apontar; textura = no objeto sob o mouse.
    const auto meshes = AcceptPathDrop(AssetType::Mesh);
    const auto textures = AcceptPathDrop(AssetType::Texture);
    if (meshes.empty() && textures.empty()) return;

    const ImVec2 mouse = ImGui::GetMousePos();
    const auto [rayOrigin, rayDir] =
        ViewportRay({(mouse.x - origin.x) / m_viewportSize.x, (mouse.y - origin.y) / m_viewportSize.y});
    const auto hit = m_ctx.scene.Raycast(rayOrigin, rayDir);

    if (!textures.empty()) {
        if (hit) {
            m_ctx.ApplyTextureFile(hit->entity, textures.front());
        } else {
            m_ctx.pendingError = "Solte a textura sobre um objeto da cena.";
        }
    }

    // Ponto de apoio: superfície atingida, senão o chão (y = 0), senão 10 m à frente.
    glm::vec3 point = rayOrigin + glm::normalize(rayDir) * 10.0f;
    if (hit) {
        point = rayOrigin + rayDir * hit->distance;
    } else if (rayDir.y < -1e-4f) {
        const float t = -rayOrigin.y / rayDir.y;
        if (glm::length(rayDir * t) < 200.0f) point = rayOrigin + rayDir * t;
    }

    float offset = 0.0f;
    for (const auto& file : meshes) {
        if (Entity* e = m_ctx.CreateFromMeshFile(file)) {
            const Mesh& mesh = *e->Get<MeshRenderer>()->mesh;
            // Apoia a base da malha no ponto (a origem do OBJ nem sempre fica embaixo).
            e->transform.position = point + glm::vec3(offset, -mesh.BoundsMin().y, 0.0f);
            offset += std::max(1.0f, mesh.BoundsMax().x - mesh.BoundsMin().x) * 1.2f;
        }
    }
}

void EditorApp::FocusSelected() {
    if (!m_ctx.scene.Find(m_ctx.selected)) return;

    const glm::mat4 world = m_ctx.scene.WorldMatrix(m_ctx.selected);
    const glm::vec3 target{world[3]};
    const float size = std::max({glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])),
                                 glm::length(glm::vec3(world[2]))});
    const float distance = std::clamp(size * 2.5f, 2.0f, 60.0f);
    m_camera.position = target - m_camera.Forward() * distance;
}

} // namespace editor
