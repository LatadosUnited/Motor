// Documento do editor: configurações do jogo (Arquivo > Configurações do jogo, Fase 21), gravadas no .mproject.
// Identidade do jogo exportado (nome do .exe, versão, ícone) e as opções com que ele abre da primeira vez (tela
// cheia, resolução, vsync, MSAA, sombras). Depois disso valem as do jogador (%APPDATA%\<Jogo>\opcoes.json),
// que os scripts mudam com Script::ApplyOptions.

#include "document/SceneDocument.h"

#include <motor/core/Window.h>

#include <imgui.h>
#include <imgui_stdlib.h>

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

bool SceneDocument::SetGameIdentity(const GameIdentity& identity, std::string& error) {
    if (!m_project) {
        error = "nenhum projeto aberto";
        return false;
    }
    if (!identity.executable.empty()) {
        if (std::string problem = Project::ValidateName(identity.executable); !problem.empty()) {
            error = "nome do executável: " + problem;
            return false;
        }
    }
    int parts[4];
    if (!ParseGameVersion(identity.version, parts)) {
        error = "a versão precisa ser números separados por ponto (ex.: 1.0.0)";
        return false;
    }
    if (!identity.loadingScene.empty()) {
        std::error_code ec;
        if (PathFromUtf8(identity.loadingScene).extension() != ".mscene" ||
            !std::filesystem::is_regular_file(m_project->ToAbsolute(identity.loadingScene), ec)) {
            error = "a tela de carregamento precisa ser uma cena (.mscene) do projeto";
            return false;
        }
    }
    if (!identity.icon.empty()) {
        const std::string ext = ToUtf8(PathFromUtf8(identity.icon).extension());
        std::error_code ec;
        if ((ext != ".png" && ext != ".ico") || !std::filesystem::is_regular_file(m_project->ToAbsolute(identity.icon), ec)) {
            error = "o ícone precisa ser um arquivo .png ou .ico do projeto";
            return false;
        }
    }
    const GameIdentity before = m_project->Identity();
    m_project->SetIdentity(identity);
    if (!m_project->Save(&error)) {
        m_project->SetIdentity(before);
        return false;
    }
    return true;
}

void SceneDocument::DrawGameIdentity() {
    // Os campos de texto editam uma cópia, gravada ao sair do campo (não a cada tecla).
    if (!m_identityEditing) m_identityDraft = m_project->Identity();
    GameIdentity& draft = m_identityDraft;
    bool commit = false;

    ImGui::SeparatorText("Identidade");
    ImGui::InputTextWithHint("Executável", m_project->Name().c_str(), &draft.executable);
    bool editing = ImGui::IsItemActive();
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetItemTooltip("Nome do .exe exportado (sem \".exe\"). Vazio = o nome do projeto.");
    ImGui::InputText("Versão", &draft.version);
    editing |= ImGui::IsItemActive();
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetItemTooltip("Aparece nas propriedades do .exe e em Script::GameVersion. Ex.: 1.0.0");
    m_identityEditing = editing;

    // Ícone: um .png (até 256x256) ou .ico de Assets/.
    const std::string iconLabel = draft.icon.empty() ? std::string("(nenhum)") : draft.icon;
    if (ImGui::BeginCombo("Ícone", iconLabel.c_str())) {
        if (ImGui::Selectable("(nenhum)", draft.icon.empty())) {
            draft.icon.clear();
            commit = true;
        }
        std::vector<std::string> icons;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(m_project->AssetsDir(), ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            const std::string ext = ToUtf8(it->path().extension());
            if (it->is_regular_file(ec) && (ext == ".png" || ext == ".ico")) icons.push_back(m_project->ToRelative(it->path()));
        }
        std::sort(icons.begin(), icons.end());
        for (const std::string& icon : icons) {
            if (ImGui::Selectable(icon.c_str(), icon == draft.icon)) {
                draft.icon = icon;
                commit = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Ícone do .exe, da janela e da barra de tarefas. Use um .ico com vários tamanhos ou um "
                          ".png de 256x256.");

    // Tela de carregamento: uma cena do projeto (UI, e se quiser câmera e objetos), desenhada parada.
    const std::string loadingLabel = draft.loadingScene.empty() ? std::string("(padrão: \"Carregando...\")")
                                                                : draft.loadingScene;
    if (ImGui::BeginCombo("Tela de carregamento", loadingLabel.c_str())) {
        if (ImGui::Selectable("(padrão: \"Carregando...\")", draft.loadingScene.empty())) {
            draft.loadingScene.clear();
            commit = true;
        }
        std::vector<std::string> scenes;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(m_project->AssetsDir(), ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (it->is_regular_file(ec) && it->path().extension() == ".mscene") scenes.push_back(m_project->ToRelative(it->path()));
        }
        std::sort(scenes.begin(), scenes.end());
        for (const std::string& scene : scenes) {
            if (ImGui::Selectable(scene.c_str(), scene == draft.loadingScene)) {
                draft.loadingScene = scene;
                commit = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Mostrada no jogo exportado enquanto ele abre e a cada troca de cena (LoadScene). É uma cena "
                          "comum, sem scripts rodando: faça com UI (imagem, texto) e, se quiser, uma câmera.");

    if (commit && !(draft == m_project->Identity())) {
        std::string error;
        if (!SetGameIdentity(draft, error)) {
            ShowError("Não foi possível salvar a identidade do jogo: " + error);
            m_identityDraft = m_project->Identity();
        }
    }
}

void SceneDocument::DrawGameSettingsWindow() {
    if (!m_showGameSettingsWindow) return;
    ImGui::SetNextWindowSize(ImVec2(480.0f, 430.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Configurações do jogo", &m_showGameSettingsWindow)) {
        ImGui::End();
        return;
    }
    if (!m_project) {
        ImGui::TextDisabled("Abra um projeto para editar as configurações do jogo.");
        ImGui::End();
        return;
    }

    DrawGameIdentity();

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
