// Documento do editor: configurações do jogo (Arquivo > Configurações do jogo, Fase 21). Opções com que o jogo
// exportado abre da primeira vez (tela cheia, resolução, vsync, MSAA, sombras), gravadas no .mproject. Depois
// disso valem as do jogador (%APPDATA%\<Jogo>\opcoes.json), que os scripts mudam com Script::ApplyOptions.

#include "document/SceneDocument.h"

#include <motor/core/Window.h>

#include <imgui.h>

#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace editor {

using namespace motor;

bool SceneDocument::SetGameOptions(const GameOptions& options, std::string& error) {
    if (!m_project) {
        error = "nenhum projeto aberto";
        return false;
    }
    const GameOptions before = m_project->DefaultOptions();
    GameOptions clamped = options;
    clamped.Clamp();
    m_project->SetDefaultOptions(clamped);
    if (!m_project->Save(&error)) {
        m_project->SetDefaultOptions(before);
        return false;
    }
    return true;
}

void SceneDocument::DrawGameSettingsWindow() {
    if (!m_showGameSettingsWindow) return;
    ImGui::SetNextWindowSize(ImVec2(460.0f, 320.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Configurações do jogo", &m_showGameSettingsWindow)) {
        ImGui::End();
        return;
    }
    if (!m_project) {
        ImGui::TextDisabled("Abra um projeto para editar as configurações do jogo.");
        ImGui::End();
        return;
    }

    GameOptions options = m_project->DefaultOptions();
    bool changed = false;

    ImGui::SeparatorText("Opções iniciais do jogador");
    ImGui::TextWrapped("Com estas opções o jogo exportado abre da primeira vez. Depois valem as que o jogador "
                       "escolher no menu de opções do jogo (Script::ApplyOptions).");
    changed |= ImGui::Checkbox("Tela cheia", &options.fullscreen);

    const auto label = [&](int w, int h) {
        if (w == 0) return std::string(options.fullscreen ? "A da tela" : "Padrão (1280x720)");
        return std::format("{}x{}", w, h);
    };
    if (ImGui::BeginCombo("Resolução", label(options.width, options.height).c_str())) {
        std::vector<VideoMode> modes = Window::MonitorModes();
        if (!options.fullscreen) {
            for (VideoMode m : {VideoMode{1280, 720}, VideoMode{1600, 900}, VideoMode{1920, 1080}}) {
                if (std::find(modes.begin(), modes.end(), m) == modes.end()) modes.push_back(m);
            }
        }
        modes.insert(modes.begin(), VideoMode{0, 0});
        for (const VideoMode& m : modes) {
            const bool selected = m.width == options.width && m.height == options.height;
            if (ImGui::Selectable(label(m.width, m.height).c_str(), selected)) {
                options.width = m.width;
                options.height = m.height;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    changed |= ImGui::Checkbox("Vsync", &options.vsync);
    ImGui::SetItemTooltip("Sincroniza com a tela: sem cortes na imagem, no máximo a taxa do monitor.");

    const std::string msaa = options.msaa <= 1 ? std::string("Desligado") : std::format("{}x", options.msaa);
    if (ImGui::BeginCombo("MSAA", msaa.c_str())) {
        for (int choice : GameOptions::kMsaaChoices) {
            const std::string text = choice <= 1 ? std::string("Desligado") : std::format("{}x", choice);
            if (ImGui::Selectable(text.c_str(), choice == options.msaa)) {
                options.msaa = choice;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Suaviza as bordas dos objetos. Mais amostras pesam mais na placa de vídeo.");
    if (ImGui::BeginCombo("Sombras", ShadowSettingLabel(options.shadows))) {
        for (ShadowSetting s : {ShadowSetting::Off, ShadowSetting::Low, ShadowSetting::Medium, ShadowSetting::High}) {
            if (ImGui::Selectable(ShadowSettingLabel(s), s == options.shadows)) {
                options.shadows = s;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Limite da qualidade das sombras de cada luz (Desligadas tira todas).");
    ImGui::TextDisabled("No Play do editor valem o MSAA e as sombras; tela cheia, resolução e vsync só no jogo exportado.");

    if (changed && !(options == m_project->DefaultOptions())) {
        std::string error;
        if (!SetGameOptions(options, error)) ShowError("Não foi possível salvar as configurações do jogo: " + error);
    }
    ImGui::End();
}

} // namespace editor
