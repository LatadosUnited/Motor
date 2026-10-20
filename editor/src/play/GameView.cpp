#include "play/GameView.h"

#include "play/PlayController.h"

#include <imgui_internal.h>  // DockBuilder

#include <algorithm>
#include <cstdint>
#include <utility>

namespace editor {

using namespace motor;

namespace {
constexpr const char* kWindowName = "Jogo";
constexpr const char* kViewportWindow = "Viewport";
} // namespace

void GameView::Init() {
    m_framebuffer = std::make_unique<Framebuffer>(1280, 720, 1);
    m_ui = std::make_unique<UiRenderer>();
}

void GameView::PrepareDock(const PlayController& play) {
    const bool on = play.SideBySide();
    if (on && !m_wasSideBySide) {
        // Já tem lugar salvo (imgui.ini de outra sessão): fica onde estava.
        const ImGuiWindowSettings* saved = ImGui::FindWindowSettingsByID(ImHashStr(kWindowName));
        const ImGuiWindow* window = ImGui::FindWindowByName(kWindowName);
        const bool docked = (window && window->DockId != 0) || (!window && saved && saved->DockId != 0);
        m_dockPending = !docked;
    }
    m_wasSideBySide = on;
    if (!on || !m_dockPending) return;

    const ImGuiWindow* viewport = ImGui::FindWindowByName(kViewportWindow);
    if (!viewport || !viewport->DockNode) return;  // o Viewport ainda não foi desenhado: tenta no próximo quadro
    ImGuiID left = 0;
    const ImGuiID right = ImGui::DockBuilderSplitNode(viewport->DockNode->ID, ImGuiDir_Right, 0.5f, nullptr, &left);
    ImGui::DockBuilderDockWindow(kWindowName, right);
    ImGui::DockBuilderFinish(ImGui::DockNodeGetRootNode(viewport->DockNode)->ID);
    m_dockPending = false;
}

void GameView::Render(Renderer& renderer, const EditorContext& ctx) {
    if (!m_visible) return;
    m_framebuffer->Resize(static_cast<int>(m_size.x), static_cast<int>(m_size.y));
    m_missingCamera = !RenderTo(renderer, ctx, *m_framebuffer);
}

bool GameView::RenderTo(Renderer& renderer, const EditorContext& ctx, const Framebuffer& target) {
    if (!m_ui) m_ui = std::make_unique<UiRenderer>();
    const Scene& scene = ctx.scene;
    Camera camera;
    camera.aspect = target.AspectRatio();
    const bool missingCamera = !scene.ApplyCamera(scene.FindMainCamera(), camera);
    renderer.BeginScene(camera, scene.CollectLights(), scene.environment, &target);
    if (!missingCamera) scene.SubmitTo(renderer);
    renderer.EndScene();
    if (!missingCamera) m_ui->Render(scene, ctx.assets, target.Width(), target.Height(), &target);
    return !missingCamera;
}

void GameView::Draw(PlayController& play) {
    m_visible = false;
    m_focused = false;
    if (!play.SideBySide()) return;

    if (std::exchange(m_focusRequest, false)) ImGui::SetNextWindowFocus();
    bool open = true;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool shown = ImGui::Begin(kWindowName, &open);
    ImGui::PopStyleVar();
    if (!open) play.SetSideBySide(false);  // fechar a janela desliga o modo
    if (!shown) {
        ImGui::End();
        return;
    }
    m_visible = true;
    m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    m_size = {std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const auto texture = static_cast<ImTextureID>(static_cast<uintptr_t>(m_framebuffer->ColorTexture()));
    ImGui::Image(ImTextureRef(texture), ImVec2(m_size.x, m_size.y), ImVec2(0, 1), ImVec2(1, 0));  // V invertido
    const bool hovered = ImGui::IsItemHovered();
    // Clique na imagem dá foco à janela (o jogo passa a receber a entrada), como no Viewport.
    if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        ImGui::SetWindowFocus();
    }

    // Mouse para a UI do jogo (botões): nesta janela, no lugar do Viewport.
    if (SceneRuntime* runtime = play.Runtime()) {
        if (UiSystem* ui = runtime->GetSystem<UiSystem>()) {
            const ImVec2 mouse = ImGui::GetMousePos();
            ui->SetPointer({m_size, {mouse.x - origin.x, mouse.y - origin.y}, hovered,
                            ImGui::IsMouseDown(ImGuiMouseButton_Left)});
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end{origin.x + m_size.x, origin.y + m_size.y};
    if (play.IsActive()) {
        dl->AddRect(ImVec2(origin.x + 1.5f, origin.y + 1.5f), ImVec2(end.x - 1.5f, end.y - 1.5f), play.StateColor(),
                    0.0f, 0, 3.0f);
    }
    const char* note = nullptr;
    if (m_missingCamera) {
        note = "Nenhuma câmera ativa na cena (adicione o componente Câmera)";
    } else if (play.IsActive() && !m_focused) {
        note = "Clique aqui para jogar (o teclado e o mouse vão para o jogo)";
    }
    if (note) {
        const ImVec2 ts = ImGui::CalcTextSize(note);
        const ImVec2 p{origin.x + (m_size.x - ts.x) * 0.5f, end.y - ts.y - 16.0f};
        dl->AddRectFilled(ImVec2(p.x - 8.0f, p.y - 5.0f), ImVec2(p.x + ts.x + 8.0f, p.y + ts.y + 5.0f),
                          IM_COL32(0, 0, 0, 150), 4.0f);
        dl->AddText(p, IM_COL32(255, 220, 120, 255), note);
    }
    ImGui::End();
}

} // namespace editor
