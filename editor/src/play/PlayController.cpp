#include "play/PlayController.h"

#include <motor/app/GameQuality.h>
#include <motor/core/Log.h>

#include <algorithm>
#include <format>
#include <utility>

namespace editor {

using namespace motor;

namespace {
constexpr ImU32 kPlayColor = IM_COL32(70, 150, 255, 255);   // borda do viewport jogando
constexpr ImU32 kPauseColor = IM_COL32(255, 170, 40, 255);  // e em pausa
} // namespace

ImU32 PlayController::StateColor() const {
    return m_mode.IsPaused() ? kPauseColor : kPlayColor;
}

PlayController::Event PlayController::ApplyRequest(EditorContext& ctx, UndoHistory& history) {
    switch (std::exchange(m_request, Request::None)) {
        case Request::None: break;
        case Request::Play:
            if (m_mode.IsActive()) {
                m_mode.SetPaused(false);  // Play em pausa = continuar
                break;
            }
            // Fecha alguma alteração ainda não registrada: é para esse estado que o Parar volta.
            history.Track(ctx.scene, ctx.selected, true);
            ctx.pendingDelete = kNullEntity;
            m_gameView = true;
            // Script::LoadScene: a cena pedida é procurada no projeto e carregada no lugar da atual.
            m_mode.SetSceneLoader([&ctx](const std::string& request, Scene& out, std::string& error) {
                const std::filesystem::path file = FindSceneFile(ctx.assets, request);
                if (file.empty()) {
                    error = "cena não encontrada no projeto";
                    return false;
                }
                ctx.selected = kNullEntity;  // a seleção era da cena que sai
                return SceneSerializer::Load(file, out, ctx.assets, &error);
            });
            m_mode.SetAssets(&ctx.assets);
            m_host.Reset(m_gameInfo.options, m_gameInfo.version, m_gameInfo.savesFolder);
            m_host.SetActions(m_gameInfo.actions);
            m_mode.SetHost(&m_host);
            m_mode.Play(ctx.scene, ctx.selected);
            return Event::Started;
        case Request::Pause: m_mode.SetPaused(!m_mode.IsPaused()); break;
        case Request::Stop: return Stop(ctx) ? Event::Stopped : Event::None;
        case Request::Step: m_mode.Step(1.0f / 60.0f); break;
    }
    return Event::None;
}

void PlayController::ApplyQuality(Renderer& renderer) const {
    ApplyGameQuality(renderer, m_mode.IsActive() ? m_host.Options() : GameOptions{});
}

bool PlayController::Stop(EditorContext& ctx) {
    if (!m_mode.IsActive()) return false;
    m_mode.Stop(ctx.scene, ctx.selected);
    m_host.Saves().SaveAll();  // o que os scripts mudaram e não gravaram (também no OnDestroy)
    ctx.pendingDelete = kNullEntity;
    ctx.ForgetAngles();  // o Inspector recalcula os ângulos da cena restaurada
    return true;
}

void PlayController::Update(float dt, bool viewportFocused, bool gameWindowFocused, motor::Window& window) {
    // O jogo só lê teclado/mouse com a janela que o mostra em foco e sem campo de texto ativo. Entrada externa
    // (teclas mandadas pelo servidor MCP) vale sem foco: ela não vem do teclado de verdade.
    m_gameFocused = m_mode.IsActive() && !ImGui::GetIO().WantTextInput &&
                    (m_sideBySide ? gameWindowFocused : InGameView() && viewportFocused);
    if (SceneRuntime* runtime = m_mode.Runtime()) runtime->SetInputEnabled(m_externalInput || m_gameFocused);
    m_mode.Update(dt);
    if (m_host.TakeQuit() && m_mode.IsActive()) {
        log::Info("QuitGame: o jogo pediu para fechar; parando o Play");
        m_request = Request::Stop;
    }
    UpdateCursor(window);
}

void PlayController::UpdateCursor(motor::Window& window) {
    using motor::CursorMode;
    motor::SceneRuntime* runtime = m_mode.Runtime();
    const CursorMode requested = runtime ? runtime->GetCursorMode() : CursorMode::Normal;
    const bool gameHasMouse = runtime && m_gameFocused && !m_mode.IsPaused();
    if (!runtime || requested == CursorMode::Normal) {
        m_cursorReleased = false;
    } else if (gameHasMouse && !m_cursorReleased && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        m_cursorReleased = true;  // como na Unity: Esc devolve o mouse ao editor
    } else if (m_cursorReleased && gameHasMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_cursorReleased = false;  // clique no jogo prende de novo
    }
    const CursorMode want = gameHasMouse && !m_cursorReleased ? requested : CursorMode::Normal;
    if (want == m_appliedCursor) return;
    window.SetCursorMode(want);
    // Cursor preso: o ImGui não deve reagir ao mouse (a posição dele fica sem sentido).
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiConfigFlags mouseFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
    if (want == CursorMode::Locked) io.ConfigFlags |= mouseFlags;
    else io.ConfigFlags &= ~mouseFlags;
    m_appliedCursor = want;
}

int PlayController::DeletePlaySaves() {
    SaveStore store(m_gameInfo.savesFolder);
    int count = 0;
    for (const std::string& slot : store.List()) count += store.Delete(slot) ? 1 : 0;
    if (m_mode.IsActive()) m_host.Saves().Forget();
    log::Info("Saves do Play apagados: {}", count);
    return count;
}

void PlayController::DrawMenu() {
    if (!ImGui::BeginMenu("Jogo")) return;
    const bool active = m_mode.IsActive();
    if (ImGui::MenuItem(active && m_mode.IsPaused() ? "Continuar" : "Play", "Ctrl+P", false,
                        !active || m_mode.IsPaused())) {
        m_request = Request::Play;
    }
    if (ImGui::MenuItem("Pausa", "Ctrl+Shift+P", m_mode.IsPaused(), active)) m_request = Request::Pause;
    if (ImGui::MenuItem("Avançar um quadro", nullptr, false, m_mode.IsPaused())) m_request = Request::Step;
    if (ImGui::MenuItem("Parar", "Ctrl+P", false, active)) m_request = Request::Stop;
    ImGui::Separator();
    if (ImGui::MenuItem("Ver pela câmera do jogo", "F8", m_gameView && !m_sideBySide, active && !m_sideBySide)) {
        m_gameView = !m_gameView;
    }
    if (ImGui::MenuItem("Cena e Jogo lado a lado", nullptr, m_sideBySide)) m_sideBySide = !m_sideBySide;
    ImGui::SetItemTooltip("Janela Jogo ao lado do Viewport: o jogo roda nela (clique para jogar) e o Viewport\n"
                          "continua na câmera do editor, para mexer na cena enquanto joga.");
    ImGui::Separator();
    const int saves = m_gameInfo.savesFolder.empty() ? 0 : static_cast<int>(SaveStore(m_gameInfo.savesFolder).List().size());
    if (ImGui::MenuItem(saves > 0 ? std::format("Apagar saves do Play ({})", saves).c_str() : "Apagar saves do Play",
                        nullptr, false, saves > 0)) {
        DeletePlaySaves();
    }
    ImGui::SetItemTooltip("Os saves dos scripts (SaveSlot) no Play ficam em <projeto>/.motor/saves, separados dos\n"
                          "do jogo exportado. Apagar = começar como um jogador novo.");
    ImGui::EndMenu();
}

void PlayController::DrawStatus() {
    if (!m_mode.IsActive()) return;
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, StateColor());
    ImGui::TextUnformatted(m_mode.IsPaused() ? "EM PAUSA" : "JOGANDO");
    ImGui::PopStyleColor();
    ImGui::TextDisabled("(alterações feitas agora são descartadas ao parar)");
}

float PlayController::DrawControls(const ImVec2& origin, float viewportWidth) {
    enum class Icon { Play, Pause, Step, Stop };
    const bool active = m_mode.IsActive();
    const bool paused = m_mode.IsPaused();
    const float size = ImGui::GetFrameHeight() + 6.0f;
    const float spacing = 4.0f;
    const char* viewLabel = m_gameView ? "Câmera: jogo" : "Câmera: editor";
    const bool viewButton = active && !m_sideBySide;  // lado a lado, o jogo já tem a janela dele
    const float viewWidth = viewButton ? ImGui::CalcTextSize(viewLabel).x + ImGui::GetStyle().FramePadding.x * 2 : 0.0f;
    const float width = size * 4 + spacing * 3 + (viewButton ? spacing * 3 + viewWidth : 0.0f);

    // Botões quadrados com o ícone desenhado (a fonte não tem os símbolos ▶ ⏸ ⏹).
    const ImVec4 activeColor = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    auto iconButton = [&](const char* id, Icon icon, bool on, bool enabled, const char* tip) {
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::Button(id, ImVec2(size, size));
        ImGui::EndDisabled();
        if (on) ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", tip);

        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const ImVec2 c{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
        const float r = size * 0.24f;
        const ImU32 col = enabled ? IM_COL32(240, 240, 240, 255) : IM_COL32(120, 120, 120, 255);
        ImDrawList* idl = ImGui::GetWindowDrawList();
        switch (icon) {
            case Icon::Play:
                idl->AddTriangleFilled(ImVec2(c.x - r * 0.8f, c.y - r), ImVec2(c.x - r * 0.8f, c.y + r),
                                       ImVec2(c.x + r, c.y), col);
                break;
            case Icon::Pause:
                idl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x - r * 0.3f, c.y + r), col);
                idl->AddRectFilled(ImVec2(c.x + r * 0.3f, c.y - r), ImVec2(c.x + r, c.y + r), col);
                break;
            case Icon::Step:
                idl->AddTriangleFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x - r, c.y + r), ImVec2(c.x + r * 0.5f, c.y), col);
                idl->AddRectFilled(ImVec2(c.x + r * 0.6f, c.y - r), ImVec2(c.x + r, c.y + r), col);
                break;
            case Icon::Stop:
                idl->AddRectFilled(ImVec2(c.x - r * 0.9f, c.y - r * 0.9f), ImVec2(c.x + r * 0.9f, c.y + r * 0.9f), col);
                break;
        }
        return clicked;
    };

    const ImVec2 position{origin.x + std::max(8.0f, (viewportWidth - width) * 0.5f), origin.y + 8.0f};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::SetCursorScreenPos(ImVec2(position.x + 5.0f, position.y + 5.0f));
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));

    if (iconButton("##play", Icon::Play, active && !paused, !active || paused,
                   active ? "Continuar (Ctrl+P para parar)" : "Play (Ctrl+P)")) {
        m_request = Request::Play;
    }
    ImGui::SameLine();
    if (iconButton("##pause", Icon::Pause, paused, active, "Pausa (Ctrl+Shift+P)")) m_request = Request::Pause;
    ImGui::SameLine();
    if (iconButton("##step", Icon::Step, false, paused, "Avançar um quadro (em pausa)")) m_request = Request::Step;
    ImGui::SameLine();
    if (iconButton("##stop", Icon::Stop, false, active, "Parar e voltar a cena ao estado de antes do Play (Ctrl+P)")) {
        m_request = Request::Stop;
    }
    if (viewButton) {
        ImGui::SameLine(0.0f, spacing * 4);
        if (ImGui::Button(viewLabel, ImVec2(0.0f, size))) m_gameView = !m_gameView;
        ImGui::SetItemTooltip("Alterna entre a câmera do jogo e a câmera livre do editor (F8)");
    }

    ImGui::PopStyleVar();
    ImGui::EndGroup();

    const ImVec2 groupMax = ImGui::GetItemRectMax();
    dl->ChannelsSetCurrent(0);
    const ImU32 bg = active ? (paused ? IM_COL32(110, 70, 10, 170) : IM_COL32(20, 60, 120, 170)) : IM_COL32(0, 0, 0, 120);
    dl->AddRectFilled(position, ImVec2(groupMax.x + 5.0f, groupMax.y + 5.0f), bg, 4.0f);
    dl->ChannelsMerge();
    return position.x;
}

} // namespace editor
